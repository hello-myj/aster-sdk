"""Shared identity for source SDK and the three frozen Windows tools."""
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from runtime_paths import sdk_root


def source_identity(root):
    digest = hashlib.sha256()
    for folder in ('tools', 'contract', 'targets', 'include', 'lib', 'templates', 'toolchains', 'docs', '.agents', 'apps', 'assets'):
        # WindowsPath compares case-insensitively; PosixPath does not. Hash the
        # same explicit relative-name ordering on every build/validation host.
        for path in sorted((root / folder).rglob('*'), key=lambda p:p.relative_to(root).as_posix()):
            if path.is_file() and not any(part in ('__pycache__', 'build', 'dist') for part in path.relative_to(root).parts) and path.suffix != '.pyc':
                name = path.relative_to(root).as_posix().encode()
                content = path.read_bytes()
                if path.suffix in ('.py', '.ps1', '.sh', '.json', '.h', '.c', '.toml', '.md', '.txt'):
                    content = content.replace(b'\r\n', b'\n')
                digest.update(name + b'\0' + hashlib.sha256(content).digest())
    for name in ('requirements.txt', 'requirements-windows.txt'):
        digest.update(name.encode() + (root / name).read_bytes().replace(b'\r\n', b'\n'))
    for name in ('release.sh', 'README.md', 'LICENSE', 'NOTICE.md'):
        if (root/name).is_file():
            digest.update(name.encode()+(root/name).read_bytes().replace(b'\r\n',b'\n'))
    try:
        commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], stderr=subprocess.DEVNULL, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        commit = 'unknown'
    commit = os.environ.get('ASTER_RELEASE_COMMIT', commit)
    abi = json.loads((root / 'contract/lock.json').read_text(encoding='utf-8'))['abi']
    return {'schema': 1, 'product': 'aster', 'abi': abi, 'commit': commit,
            'build_id': digest.hexdigest()}


def identity():
    if getattr(sys, 'frozen', False) or (sdk_root() / 'release.json').is_file():
        return json.loads((sdk_root() / 'release.json').read_text(encoding='utf-8'))
    return source_identity(sdk_root())


def version_requested(argv):
    if argv not in (['--version'], ['--version-json']):
        return False
    info = identity()
    if argv == ['--version-json']:
        print(json.dumps(info, sort_keys=True))
    else:
        print(f"Aster SDK ABI {info['abi']} commit {info['commit'][:12]} build {info['build_id'][:16]}")
    return True


def check_sibling(path):
    try:
        result = subprocess.run([str(path), '--version-json'], capture_output=True, text=True, timeout=30, check=True)
        other = json.loads(result.stdout)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise RuntimeError(f'{path.name}: cannot read build identity; replace all three EXEs from one release') from error
    expected = identity()
    if any(other.get(k) != expected[k] for k in ('product', 'abi', 'build_id', 'commit')):
        raise RuntimeError(f'{path.name}: mixed SDK releases; replace all three EXEs from one release')


if __name__ == '__main__':
    destination = Path(sys.argv[1])
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(source_identity(sdk_root()), indent=2) + '\n', encoding='utf-8')
