#!/usr/bin/env python3
"""Validate current operational docs against SDK ABI and installed CPU targets."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(root=ROOT):
    root = Path(root)
    abi = json.loads((root / 'contract/lock.json').read_text())['abi']
    targets = {p.stem for p in (root / 'targets').glob('*.json')}
    errors = []
    paths = [root / 'README.md']
    for folder in ('docs', '.agents', 'apps', 'templates'):
        paths.extend((root / folder).rglob('*.md'))
    for path in sorted(set(paths)):
        if not path.is_file() or any(part in ('build', 'dist', 'node_modules') for part in path.relative_to(root).parts):
            continue
        text = path.read_text(encoding='utf-8')
        # Archived handoff records remain readable but never serve as current instructions.
        if text.startswith('<!-- aster-docs: historical -->\n'):
            if '历史' not in text.splitlines()[1] or 'release.md' not in text.splitlines()[1]:
                errors.append(f'{path.relative_to(root)}: historical document needs a visible redirect to release.md')
            continue
        for line_no, line in enumerate(text.splitlines(), 1):
            where = f'{path.relative_to(root)}:{line_no}'
            for value in re.findall(r'--target(?:\s+|=)[\x60\"\']?([A-Za-z0-9_-]+)', line):
                if value.isupper():  # Documented placeholders such as TARGET.
                    continue
                if value not in targets:
                    errors.append(f'{where}: unsupported --target {value}; available: {sorted(targets)}')
            current_versions = re.findall(r'当前\s*ABI\s*(?:为|[:：])\s*([0-9]+(?:\.[0-9]+){1,2})', line)
            if line.startswith('适用版本：SDK ABI') or line.startswith('description:') or '自包含开发参考' in line:
                current_versions += re.findall(r'(?:SDK\s+)?ABI\s*[*`]*([0-9]+(?:\.[0-9]+){1,2})', line)
            for value in current_versions:
                if tuple(map(int, value.split('.'))) != tuple(map(int, abi.split('.')))[:len(value.split('.'))]:
                    errors.append(f'{where}: current ABI {value} differs from contract {abi}')
            if re.search(r'--board(?:\s|=|[\x60])', line) and '不接受' not in line and '不提供' not in line:
                errors.append(f'{where}: SDK CLI uses --target CPU; --board is not supported')
            for value in re.findall(r'(?:dist|build)[/\\]([a-z0-9-]+)[/\\]', line):
                if value in ('lckfb-s3', 'waveshare-35b'):
                    errors.append(f'{where}: outdated board output directory {value}; use CPU target directory')
    if errors:
        raise ValueError('\n'.join(errors))
    return f'SDK docs checked: ABI {abi}, CPU targets {", ".join(sorted(targets))}'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        print(check(args.root))
    except ValueError as error:
        parser.exit(1, f'{error}\n')
