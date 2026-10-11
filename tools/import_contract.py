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
    files = platform_files(files, root)
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


def platform_files(files, root):
    """Verify the exported bundle first, then derive SDK CPU targets.

    Hardware geometry, heap ceilings and available sensors belong to firmware.
    The SDK retains only platforms with a registered compiler profile.
    """
    targets = [json.loads(data) for name, data in files.items() if name.startswith('targets/')]
    if not any(t.get('cpu') and t['id'] != t['cpu'] for t in targets):
        return files
    api = json.loads(files['contract/api.json'])
    catalog_path = root / 'toolchains/catalog.json'
    if not catalog_path.exists():
        catalog_path = ROOT / 'toolchains/catalog.json'
    catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
    result = {name: data for name, data in files.items()
              if not name.startswith('targets/') and name != 'contract/lock.json'}
    platforms = 0
    for cpu, compiler in catalog['boards'].items():
        sources = [t for t in targets if t.get('cpu') == cpu]
        if not sources:
            continue
        backends = {t.get('aot_target') for t in sources}
        if backends != {compiler['aot_backend']}:
            raise ValueError('firmware AOT backend differs from SDK compiler: ' + cpu)
        path = root / ('targets/' + cpu + '.json')
        existing = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
        capabilities = list(api['feature_bits'])
        # Keep stable ordering when refreshing an existing platform release.
        ordered = [c for c in existing.get('capabilities', []) if c in capabilities]
        ordered.extend(c for c in capabilities if c not in ordered)
        target = {'schema': 1, 'id': cpu, 'product': api['product'], 'abi': api['abi'],
                  'system': sources[0]['system'], 'cpu': cpu,
                  'aot_target': compiler['aot_backend'], 'capabilities': ordered,
                  'deployment': existing.get('deployment', {'transport': 'serial',
                      'console': '/dev/ttyUSB0', 'data_glob': '/dev/ttyACM*', 'package_dir': '/SD:'}),
                  'status': 'build-supported', 'name': existing.get('name', cpu.capitalize())}
        result['targets/' + cpu + '.json'] = (json.dumps(target, indent=2) + '\n').encode()
        platforms += 1
    if not platforms:
        raise ValueError('firmware bundle has no platform supported by SDK compilers')
    lock = {'schema': 1, 'product': api['product'], 'abi': api['abi'],
            'sha256': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(result.items())}}
    result['contract/lock.json'] = (json.dumps(lock, indent=2) + '\n').encode()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    import_bundle(args.bundle)
    print("Imported contract; review and commit include/, contract/, targets/ together.")


if __name__ == "__main__":
    main()
