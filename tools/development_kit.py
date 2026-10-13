"""Stage pinned firmware and simulator binaries; APP builds never use this tool."""
import hashlib
import json
import shutil
import tarfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath


def stage(root, out, host, cache, bundles=None):
    document = root / 'contract/release-family.json'
    if not document.is_file():
        return
    family = json.loads(document.read_text())
    abi = json.loads((root / 'contract/lock.json').read_text())['abi']
    if family['schema'] != 1 or family['abi'] != abi:
        raise ValueError('Development kit ABI differs from SDK')
    if family.get('contract_api_sha256') and hashlib.sha256((root / 'contract/api.json').read_bytes()).hexdigest() != family['contract_api_sha256']:
        raise ValueError('Development kit public API differs from SDK')
    for component in family['components'].values():
        if component['abi'] != abi or len(component['revision']) != 40:
            raise ValueError('Development kit component identity differs from SDK')
    record = family['payloads'][host]
    name = record['file']
    if Path(name).name != name or '\\' in name:
        raise ValueError('Unsafe development kit filename')
    directory = Path(bundles) if bundles else cache / 'development-kit'
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / name
    if not archive.exists():
        if bundles:
            raise ValueError('Missing pinned development kit: ' + str(archive))
        temporary = archive.with_name(name + '.partial')
        try:
            urllib.request.urlretrieve(family['download_base'] + '/' + name, temporary)
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != record['sha256']:
        raise ValueError('Development kit checksum mismatch: ' + name)

    seen = set()
    def validate(name):
        path = PurePosixPath(name)
        if (not path.parts or path.parts[0] not in ('firmware', 'simulator') or
                path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name or
                name in seen):
            raise ValueError('Unsafe development kit member: ' + name)
        seen.add(name)

    if record['format'] == 'zip':
        with zipfile.ZipFile(archive) as zipped:
            for entry in zipped.infolist():
                validate(entry.filename)
                if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('Development kit symlinks are not allowed')
            zipped.extractall(out)
    elif record['format'] == 'tar.gz':
        with tarfile.open(archive) as packed:
            for entry in packed.getmembers():
                validate(entry.name)
                if not (entry.isfile() or entry.isdir()):
                    raise ValueError('Development kit links/devices are not allowed')
            packed.extractall(out, filter='data')
    else:
        raise ValueError('Unsupported development kit format')
    for image in family['components']['firmware']['images'].values():
        path = (out / image['path']).resolve()
        if not path.is_relative_to(out.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != image['sha256']:
            raise ValueError('Pinned firmware image checksum mismatch')
    entry = 'aster-sim.exe' if host == 'windows-x64' else 'aster-sim'
    if not (out / 'simulator' / entry).is_file():
        raise ValueError('Pinned simulator entry point missing')
    shutil.copy2(document, out / 'development-kit.json')
    info = json.loads((out / 'release.json').read_text())
    info['development_kit'] = {'family': family['family'], 'sdk_tag': family['sdk_tag'],
                               'components': family['components'], 'payload_sha256': record['sha256']}
    (out / 'release.json').write_text(json.dumps(info, indent=2) + '\n')
