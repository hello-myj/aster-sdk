#!/usr/bin/env python3
"""Assemble the native Windows SDK, development manual and Skill in one ZIP."""
import argparse
import hashlib
import json
import shutil
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXES = ('aster.exe', 'aster-toolchain.exe', 'aster-device.exe')
FOLDERS = ('docs', '.agents', 'include', 'contract', 'targets', 'lib',
           'templates', 'apps', 'assets', 'tools', 'tests', 'bin')
FILES = ('README.md', 'LICENSE', 'NOTICE.md', 'requirements.txt', 'requirements-windows.txt')
IGNORE = shutil.ignore_patterns('build', 'dist', '__pycache__', '*.pyc', '.venv*', '*.log')


def assemble(root, executables, output):
    root, executables, output = map(Path, (root, executables, output))
    for name in EXES:
        if not (executables / name).is_file():
            raise ValueError('Missing Windows executable: ' + name)
    for name in ('docs/app-development.md', '.agents/skills/aster-app-development/SKILL.md'):
        if not (root / name).is_file():
            raise ValueError('Missing development reference: ' + name)
    abi = json.loads((root / 'contract/lock.json').read_text(encoding='utf-8'))['abi']
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='aster-sdk-release-') as directory:
        sdk = Path(directory) / 'aster-sdk'
        sdk.mkdir()
        for name in EXES:
            shutil.copy2(executables / name, sdk / name)
        for name in FOLDERS:
            shutil.copytree(root / name, sdk / name, ignore=IGNORE)
        for name in FILES:
            shutil.copy2(root / name, sdk / name)
        (sdk / 'toolchain').mkdir()
        for name in ('LICENSE', 'README.md'):
            shutil.copy2(root / 'toolchain' / name, sdk / 'toolchain' / name)
        sums = {p.relative_to(sdk).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(sdk.rglob('*')) if p.is_file()}
        build_id = hashlib.sha256(json.dumps(sums, sort_keys=True).encode()).hexdigest()
        info = {'schema': 1, 'product': 'aster', 'abi': abi, 'host': 'windows-x64',
                'build_id': build_id, 'serial': {'shared_shell': True, 'protocol': 'ASTERFS 3',
                                              'legacy_dual_port': True}}
        (sdk / 'release.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
        sums['release.json'] = hashlib.sha256((sdk / 'release.json').read_bytes()).hexdigest()
        (sdk / 'SHA256SUMS.txt').write_text(''.join(f'{v}  {k}\n' for k, v in sorted(sums.items())), encoding='utf-8')
        archive = output / f'aster-sdk-{abi}-windows-x64-serial-{build_id[:12]}.zip'
        temporary = archive.with_suffix('.zip.partial')
        try:
            with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as zipped:
                for path in sorted(sdk.rglob('*')):
                    if path.is_file():
                        zipped.write(path, 'aster-sdk/' + path.relative_to(sdk).as_posix())
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
        return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executables', type=Path, default=ROOT / 'dist/windows')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/releases')
    args = parser.parse_args()
    print(assemble(ROOT, args.executables, args.output))


if __name__ == '__main__':
    main()
