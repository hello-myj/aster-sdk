"""Export/import immutable Windows compiler dependencies, independent of SDK ABI."""
import argparse
import hashlib
import json
import shutil
import tempfile
import zipfile
from pathlib import Path
from toolchain_catalog import select, check_wamrc

ROOT=Path(__file__).resolve().parents[1]


def load(bundle, destination, board):
    _, binding, _, host=select(ROOT,board,'windows-x64')
    with zipfile.ZipFile(bundle) as archive:
        if len(archive.namelist())!=len(set(archive.namelist())):
            raise ValueError('duplicate compiler bundle entries')
        info=json.loads(archive.read('bundle.json'))
        if info.get('schema')!=1 or info['profile']!=binding['profile'] or info['host']!='windows-x64':
            raise ValueError('compiler bundle does not match selected board')
        names=set(archive.namelist())
        if names!=set(info['sha256'])|{'bundle.json'}:
            raise ValueError('toolchain bundle inventory mismatch')
        content={name:archive.read(name) for name in info['sha256']}
        for name,data in content.items():
            path=(destination/name).resolve()
            if not path.is_relative_to(destination.resolve()) or '\\' in name:
                raise ValueError('unsafe compiler bundle path')
            if hashlib.sha256(data).hexdigest()!=info['sha256'][name]:
                raise ValueError('compiler bundle checksum mismatch')
        for name,data in content.items():
            path=destination/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    check_wamrc(destination/'wamrc.exe',host['wamrc_sha256'])
    return destination/'wasi',destination/'wamrc.exe'


def export(board, output):
    _, binding, _, host=select(ROOT,board,'windows-x64')
    wasi=ROOT/host['wasi'];wamrc=ROOT/host['wamrc']
    check_wamrc(wamrc,host['wamrc_sha256'])
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='toolchain-',dir=output.parent) as temp:
        root=Path(temp);shutil.copytree(wasi,root/'wasi');shutil.copy2(wamrc,root/'wamrc.exe')
        info={'schema':1,'profile':binding['profile'],'host':'windows-x64','sha256':{}}
        with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(root.rglob('*')):
                if path.is_file():
                    name=path.relative_to(root).as_posix();data=path.read_bytes()
                    info['sha256'][name]=hashlib.sha256(data).hexdigest();archive.writestr(name,data)
            archive.writestr('bundle.json',json.dumps(info,indent=2))
    print(output)
    print('SHA256:',hashlib.sha256(output.read_bytes()).hexdigest())


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',default='xtensa');parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();export(args.target,args.output)
