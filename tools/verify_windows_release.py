"""Verify identity and actual AOT build with the packaged Windows EXEs."""
import json
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path
from release_identity import identity


def verify(directory):
    root = Path(__file__).resolve().parents[1]
    expected = identity()
    for line in (directory/'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
        digest,name=line.split('  ',1)
        path=(directory/name).resolve()
        if not path.is_relative_to(directory.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            raise RuntimeError('Packaged file checksum mismatch: '+name)
    for name in ('aster', 'aster-toolchain', 'aster-device'):
        output = subprocess.check_output([str(directory / (name + '.exe')), '--version-json'], text=True)
        actual=json.loads(output)
        if any(actual.get(key)!=value for key,value in expected.items()):
            raise RuntimeError(f'{name}: packaged identity differs from current SDK')
    subprocess.run([str(directory/'runtime/python.exe'),'-c',
                    'import serial, esptool; from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey; key=Ed25519PrivateKey.generate(); key.public_key().verify(key.sign(b"release"), b"release")'],check=True)
    subprocess.run([str(directory/'aster.exe'),'install'],check=True)
    # One package serves every registered board, so the packaged EXEs must
    # build an independent project for each --target on their own.
    metadata=json.loads((directory/'release.json').read_text(encoding='utf-8'))
    boards=metadata.get('boards') or [metadata.get('board','xtensa')]
    formats={board: record['aot_format'] for record in json.loads((directory/'toolchain-profile.json').read_text())['profiles'].values() for board in record['boards']}
    with tempfile.TemporaryDirectory(prefix='aster release ') as temporary:
        cli = str(directory / 'aster.exe')
        for board in boards:
            project = Path(temporary) / board
            subprocess.run([cli, 'init', str(project), '--template', 'hello', '--id', 'test.release.hello', '--target',board], check=True)
            subprocess.run([cli, 'build', str(project), '--aot'], check=True)
            subprocess.run([str(directory/'aster-device.exe'),'build',str(project),'--target',board],check=True)
            subprocess.run([cli, 'verify', str(project / 'dist'/board/'test.release.hello.ast'), '--target', board], check=True)
            aot=(project/'build'/board/'test.release.hello/app.aot').read_bytes()
            if aot[:4]!=b'\0aot' or int.from_bytes(aot[4:8],'little')!=formats[board]:
                raise RuntimeError('AOT format differs from board profile: '+board)
    print('PASS: all tool identities match source; independent project AOT build and verify succeeded for '+', '.join(boards))


if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve())
