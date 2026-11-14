#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Export local setup/market releases to GitHub Releases or an HTTPS bucket.

No credentials are needed for export. Publishing requires the operator's gh
login or GitHub Actions GH_TOKEN; credentials never enter the exported files.
"""
import argparse
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import struct
import tempfile
from urllib.parse import urlsplit

import setup_server as setup

REPO = re.compile(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z')
TAG = re.compile(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,79}\Z')


def validate_package(path, app):
    import verify_ast
    with contextlib.redirect_stdout(io.StringIO()):
        verify_ast.check(path, board=app['target'])
    with path.open('rb') as f:
        header = f.read(64)
        offset, size = struct.unpack_from('<II', header, 12)
        if size > 65536:raise ValueError('Manifest too large')
        f.seek(offset);manifest = json.loads(f.read(size))
    for name in ('id', 'version', 'requires', 'permissions'):
        if manifest.get(name, []) != app[name]:
            raise ValueError('Market metadata differs from package: ' + name)


def load_release(root, key):
    if not setup.DIGEST.fullmatch(key):
        raise ValueError('Invalid release digest')
    path = root / 'releases' / key / 'release.json'
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != key:
        raise ValueError('Release metadata digest mismatch')
    return json.loads(raw)


def source_urls(repository, tag, origin=None):
    if origin is None:
        return (f'https://github.com/{repository}/releases/download/{tag}',
                f'https://github.com/{repository}/releases/latest/download/aster-source.json')
    parsed = urlsplit(origin)
    if (parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password
        or parsed.query or parsed.fragment or parsed.path not in ('', '/') or parsed.port
        or any(c.isspace() for c in origin)):
        raise ValueError('Expected a plain HTTPS bucket origin')
    root = origin.rstrip('/')
    base, channel = root + '/releases/' + tag, root + '/aster-source.json'
    if len(base.encode()) >= 192 or len(channel.encode()) >= 192:
        raise ValueError('Source URL exceeds device limits')
    return base, channel


def export(setup_data, store_data, output, repository, tag, boards, notices=None, origin=None):
    if not REPO.fullmatch(repository) or not TAG.fullmatch(tag) or '..' in tag:
        raise ValueError('Expected owner/repository and a simple immutable release tag')
    if output.exists():
        raise ValueError('Output already exists; choose a new directory/tag')
    if not boards or any(not setup.IDENTIFIER.fullmatch(b) or '..' in b for b in boards):
        raise ValueError('An explicit board allowlist is required')
    from aster_board import API
    bits = API['feature_bits']
    base, channel = source_urls(repository, tag, origin)
    sdk = Path(__file__).resolve().parents[1]
    records = []
    for path in sorted((store_data / 'releases').glob('*/release.json')):
        records.append((path.parent.name, load_release(store_data, path.parent.name)))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='static-', dir=output.parent) as temp:
        dest = Path(temp)

        def document(name, value, limit=16383):
            body = setup.encode(value)
            if len(body) > limit:
                raise ValueError(f'Device JSON limit exceeded: {name}')
            (dest / name).write_bytes(body)

        def blob(path, item):
            digest = item['sha256']
            if not setup.DIGEST.fullmatch(digest) or not 0 <= item['bytes'] < 2**31:
                raise ValueError('Invalid payload metadata or GitHub asset size')
            if path.is_symlink() or path.stat().st_size != item['bytes'] or setup.digest_file(path) != digest:
                raise ValueError('Payload changed or missing')
            target = dest / ('blob-' + digest)
            if not target.exists():
                shutil.copyfile(path, target)
                if setup.digest_file(target) != digest:
                    raise ValueError('Payload changed while exporting')

        exported = set()
        for board in sorted(set(boards)):
            pointer = json.loads((setup_data / 'profiles' / (board + '.json')).read_bytes())
            key = pointer['release']
            info = load_release(setup_data, key)
            target = info['target']
            if info['board'] != board or target not in ('riscv', 'xtensa') or len(info['screens']) != 1:
                raise ValueError('A static profile must bind one board/CPU/display')
            required = sum(1 << bits[f] for f in set(info['store_requires']) | {'tls'})
            abi = info['abi_min']; packed = abi[0] << 16 | abi[1] << 8 | abi[2]
            w, h = info['screens'][0]
            if len(info['files']) != 11 or info['total_bytes'] != sum(f['bytes'] for f in info['files']):
                raise ValueError('Invalid setup file list')
            plan = {'schema': 2, 'board': board, 'target': target, 'abi_min': packed,
                    'features': required, 'width': w, 'height': h, 'release': key,
                    'resource_version': info['resource_version'], 'files': 11,
                    'total_bytes': info['total_bytes'], 'market_url': channel}
            document(f'setup-{board}-{target}.json', plan, 4095)
            for i, item in enumerate(info['files']):
                expected = f'fonts/ui_{setup.SIZES[i]}.cpfont' if i < 10 else 'store/package.ast'
                if item['index'] != i or item['path'] != expected or item['kind'] != ('font' if i < 10 else 'store'):
                    raise ValueError('Invalid setup file identity')
                blob(setup_data / 'releases' / key / 'payload' / str(i), item)
                document(f'setup-{key}-{i}.json', {'schema': 1, **item, 'url': 'blob-' + item['sha256']}, 4095)

            capabilities = json.loads((sdk / 'targets' / (target + '.json')).read_bytes())['capabilities']
            features = sum({1 << bits[f] for f in capabilities})
            newest = {}
            for identity, app in records:
                minimum = app['abi_min']
                if (board not in app['boards'] or app['target'] != target or minimum[0] != abi[0]
                    or tuple(minimum) > tuple(abi) or any(not features & (1 << bits[f]) for f in app['requires'])
                    or (app['screens'] and [w, h] not in app['screens'] and [h, w] not in app['screens'])):
                    continue
                if app['id'] not in newest or setup.numeric_version(app['version']) > setup.numeric_version(newest[app['id']][1]['version']):
                    newest[app['id']] = (identity, app)
            catalog = []
            for identity, app in sorted(newest.values(), key=lambda value: value[1]['id']):
                common = {k: app[k] for k in ('id', 'version', 'boards', 'target', 'abi_min', 'requires', 'screens')}
                icon = store_data / 'icons' / (identity + '.bmp')
                art = icon.read_bytes() if icon.is_file() else None
                if art and (len(art) > 8192 or not art.startswith(b'BM')):
                    raise ValueError('Invalid exported icon')
                if art:
                    (dest / f'icon-{identity}.bmp').write_bytes(art)
                catalog.append({**common, 'release': identity, 'name': app['name'],
                    'description': app['description'], 'bytes': app['total_bytes'],
                    'file_count': len(app['files']), 'permissions': app['permissions'],
                    'icon': {'bytes': len(art), 'sha256': hashlib.sha256(art).hexdigest()} if art else None})
                if identity in exported:
                    continue
                exported.add(identity)
                validate_package(store_data / 'releases' / identity / 'payload' / '0', app)
                if not app['files'] or len(app['files']) > 4097 or sum(f['bytes'] for f in app['files']) != app['total_bytes']:
                    raise ValueError('Invalid market file list')
                for i, item in enumerate(app['files']):
                    path = item['path']
                    if (item['index'] != i or item['role'] != ('package' if i == 0 else 'resource')
                        or not path or len(path.encode()) > 95 or any(p in ('', '.', '..') for p in path.split('/'))
                        or any(ord(c) < 32 or c in '\\:"<>|?*' for c in path)):
                        raise ValueError('Unsafe market resource path')
                    blob(store_data / 'releases' / identity / 'payload' / str(i), item)
                for page in range((len(app['files']) + 7) // 8):
                    document(f'manifest-{identity}-{page}.json', {'schema': 1, 'release': identity, **common,
                        'files': app['files'][page*8:(page+1)*8], 'total': len(app['files']),
                        'next': page+1 if (page+1)*8 < len(app['files']) else -1})
            for page in range(max(1, (len(catalog) + 7) // 8)):
                document(f'catalog-{board}-{target}-{page}.json', {'schema': 1, 'board': board, 'target': target,
                    'total': len(catalog), 'items': catalog[page*8:(page+1)*8],
                    'next': page+1 if (page+1)*8 < len(catalog) else -1})
        document('aster-source.json', {'schema': 1, 'kind': 'aster-static-source', 'base_url': base}, 4095)
        if notices:(dest / 'LICENSES.txt').write_text(notices, encoding='utf8')
        (dest / 'SHA256SUMS').write_text(''.join(f'{setup.digest_file(p)}  {p.name}\n' for p in sorted(dest.iterdir())), encoding='utf8')
        if origin is None and len(list(dest.iterdir())) > 1000:
            raise ValueError('GitHub limit: more than 1000 assets; publish fewer applications in this release')
        dest.rename(output)
    return channel


def validate_export(output, repository, tag, origin=None):
    if not REPO.fullmatch(repository) or not TAG.fullmatch(tag) or '..' in tag:
        raise ValueError('Invalid repository/tag')
    if not output.is_dir() or output.is_symlink():
        raise ValueError('Expected an exported distribution directory')
    manifest = output / 'SHA256SUMS'
    if manifest.is_symlink():raise ValueError('Symlink manifest')
    expected = {}
    for line in manifest.read_text(encoding='utf8').splitlines():
        parts = line.split('  ')
        if len(parts) != 2 or not setup.DIGEST.fullmatch(parts[0]):
            raise ValueError('Invalid SHA256SUMS')
        digest, name = parts
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', name) or name in ('.', '..', 'SHA256SUMS') or name in expected:
            raise ValueError('Unsafe or duplicate release filename')
        expected[name] = digest
    if set(p.name for p in output.iterdir()) != set(expected) | {'SHA256SUMS'}:
        raise ValueError('Directory contains files outside the exported manifest')
    for name, digest in expected.items():
        path = output / name
        if path.is_symlink() or not path.is_file() or setup.digest_file(path) != digest:
            raise ValueError('Export changed: ' + name)
    source = json.loads((output / 'aster-source.json').read_bytes())
    if source != {'schema': 1, 'kind': 'aster-static-source',
                  'base_url': source_urls(repository, tag, origin)[0]}:
        raise ValueError('Export repository/tag differs from destination')


def publish(output, repository, tag):
    validate_export(output, repository, tag)
    # No shell interpolation and no token in argv. gh uses its own login or
    # GH_TOKEN supplied by the operator/Actions environment.
    existing = subprocess.run(['gh', 'release', 'view', tag, '--repo', repository], capture_output=True)
    if existing.returncode == 0:
        raise ValueError('Release already exists; assets are immutable, choose a new tag')
    subprocess.run(['gh', 'release', 'create', tag, '--repo', repository, '--draft',
                    '--title', 'Aster distribution ' + tag, '--notes',
                    'Static device resources. See the repository README and SHA256SUMS.'], check=True)
    for path in sorted(output.iterdir()):
        subprocess.run(['gh', 'release', 'upload', tag, str(path), '--repo', repository], check=True)
    subprocess.run(['gh', 'release', 'edit', tag, '--repo', repository, '--draft=false', '--latest'], check=True)


def main(argv=None):
    import sys
    argv = sys.argv[1:] if argv is None else argv
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('export')
    p.add_argument('--setup-data', type=Path, required=True);p.add_argument('--store-data', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True);p.add_argument('--board', action='append', required=True)
    p.add_argument('--repository', required=True);p.add_argument('--tag', required=True)
    p.add_argument('--origin', help='HTTPS bucket origin (omit for GitHub Releases)')
    p = sub.add_parser('publish')
    p.add_argument('--output', type=Path, required=True);p.add_argument('--repository', required=True);p.add_argument('--tag', required=True)
    args = parser.parse_args(argv)
    if args.command == 'export':
        print(export(args.setup_data, args.store_data, args.output, args.repository, args.tag, args.board, origin=args.origin))
    else:
        publish(args.output, args.repository, args.tag)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
