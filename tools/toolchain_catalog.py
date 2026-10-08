"""Bind a board contract to a separately versioned host-specific compiler profile."""
import hashlib
import json
from pathlib import Path


def select(root, board, host):
    root = Path(root)
    catalog = json.loads((root / 'toolchains/catalog.json').read_text(encoding='utf-8'))
    target = json.loads((root / 'targets' / (board+'.json')).read_text(encoding='utf-8'))
    binding = catalog['boards'].get(board)
    if not binding:
        raise ValueError(f'{board}: register a tested toolchain profile in toolchains/catalog.json')
    if target['cpu'] != binding['cpu'] or target.get('aot_target') != binding['aot_backend']:
        raise ValueError(f'{board}: firmware contract CPU/backend differs from toolchain binding')
    api = json.loads((root / 'contract/api.json').read_text(encoding='utf-8'))
    if binding['aot_backend'] not in api['aot_backends']:
        raise ValueError('toolchain backend is not part of the firmware contract')
    profile = catalog['profiles'][binding['profile']]
    if host not in profile['hosts']:
        raise ValueError(f'{board}: no tested toolchain for {host}')
    return target, binding, profile, profile['hosts'][host]


def check_wamrc(path, expected):
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    if digest != expected:
        raise ValueError(f'{path}: compiler SHA256 differs from the selected board profile')
