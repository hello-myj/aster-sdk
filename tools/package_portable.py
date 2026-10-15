"""Build Windows and Linux SDK archives from one Linux checkout, without Windows."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path
from release_identity import source_identity
from check_docs import check as check_docs
from toolchain_catalog import select, check_wamrc
from development_kit import stage as stage_development_kit
from import_contract import import_bundle
from toolchain_bundle import load as load_toolchain_bundle

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def verify_linux(out, formats):
    cli=out/'aster'
    run(cli,'install')
    for name in ('env.sh','env.ps1','env.cmd'):
        (out/name).unlink()  # Activation files must be generated at the user's final install path.
    for board, aot_format in sorted(formats.items()):
        with tempfile.TemporaryDirectory(prefix='aot smoke ', dir=out.parent) as temporary:
            project = Path(temporary)/'independent app'
            run(cli, 'init', project, '--template', 'hello', '--id', 'test.release.hello', '--target', board)
            run(cli, 'build', project, '--aot')
            run(cli, 'verify', project/'dist'/board/'test.release.hello.ast', '--target', board)
            aot=(project/'build'/board/'test.release.hello/app.aot').read_bytes()
            if aot[:4]!=b'\0aot' or int.from_bytes(aot[4:8],'little')!=aot_format:
                raise ValueError('AOT format differs from board profile')


def extract_zip(archive, destination):
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            if member.is_dir():
                continue
            path = (destination / member.filename).resolve()
            if not path.is_relative_to(destination.resolve()):
                raise ValueError('archive path escapes destination')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(z.read(member))


def copy_sdk(out, boards, platform):
    for name in ('tools', 'contract', 'include', 'lib', 'templates', 'toolchains', 'docs', '.agents', 'apps', 'assets'):
        shutil.copytree(ROOT / name, out / name, ignore=shutil.ignore_patterns('__pycache__', '*.pyc', 'build', 'dist'))
    for name in ('README.md', 'LICENSE', 'NOTICE.md', 'requirements.txt', 'requirements-windows.txt', 'requirements-assets.txt'):
        shutil.copy2(ROOT / name, out / name)
    (out / 'targets').mkdir()
    for board in boards:
        shutil.copy2(ROOT / 'targets' / (board+'.json'), out / 'targets' / (board+'.json'))
    info = source_identity(ROOT)
    info['boards'] = list(boards)
    info['host'] = platform+'-x64'
    (out / 'release.json').write_text(json.dumps(info, indent=2)+'\n')
    # Templates follow the imported firmware ABI without mutating their source originals.
    import re
    for manifest in (out/'templates').glob('*/app.toml'):
        content=manifest.read_text(encoding='utf-8')
        content=re.sub(r'^abi = .*$', 'abi = "'+info['abi']+'"', content, flags=re.M)
        content=re.sub(r'^abi_min = .*$', 'abi_min = "'+'.'.join(info['abi'].split('.')[:2])+'"', content, flags=re.M)
        manifest.write_text(content, encoding='utf-8')
    (out / 'toolchain').mkdir()
    shutil.copy2(ROOT / 'toolchain/LICENSE', out / 'toolchain/LICENSE')
    shutil.copy2(ROOT / 'toolchain/README.md', out / 'toolchain/README.md')
    api=json.loads((out/'contract/api.json').read_text(encoding='utf-8'))
    lines=['# Exported API index', '', 'ABI: '+info['abi'], '', 'Public declarations and interface comments: `include/aster/`.', '']
    for module,names in sorted(api['imports'].items()):
        lines += ['## '+module, '']
        lines += ['- `'+name+'` — `'+api['signatures'].get(name,'unspecified')+'`' for name in sorted(names)]
        lines += ['']
    (out/'docs/api-index.md').write_text('\n'.join(lines),encoding='utf-8')


def wheels(out, cache, windows):
    wheel_dir = cache / ('wheels-win313' if windows else 'wheels-linux'+str(sys.version_info.major)+str(sys.version_info.minor))
    wheel_dir.mkdir(parents=True, exist_ok=True)
    pure = cache / 'pure-wheels'
    pure.mkdir(exist_ok=True)
    if not list(pure.glob('esptool-*.whl')) or not list(pure.glob('reedsolo-*.whl')):
        run(sys.executable, '-m', 'pip', 'wheel', '--no-deps', '--wheel-dir', pure, 'esptool==5.1.0', 'reedsolo==1.7.0')
    args = [sys.executable, '-m', 'pip', 'download', '--only-binary=:all:', '--find-links', str(pure), '--dest', str(wheel_dir)]
    if windows:
        args += ['--platform', 'win_amd64', '--python-version', '313', '--implementation', 'cp', '--abi', 'cp313', '--abi', 'abi3', '--abi', 'none']
    args += ['pyserial==3.5', 'cryptography==46.0.5', 'esptool==5.1.0']
    lock=wheel_dir/'resolved.json'
    if lock.exists():
        dependencies=json.loads(lock.read_text())
        if any(not (wheel_dir/name).is_file() or hashlib.sha256((wheel_dir/name).read_bytes()).hexdigest()!=digest for name,digest in dependencies.items()):
            raise ValueError('Cached Python dependency checksum mismatch; remove the host wheel cache and retry')
        dest=out/('runtime/site-packages' if windows else 'vendor')
        for name in dependencies:
            extract_zip(wheel_dir/name,dest)
        for data in dest.glob('*.data'):
            for library in ('purelib','platlib'):
                if (data/library).exists(): shutil.copytree(data/library,dest,dirs_exist_ok=True)
        (out/'dependencies.json').write_text(json.dumps(dependencies,indent=2)+'\n')
        return
    run(*args)
    # Resolve using pip's generated selection; one manifest isolates old cached versions.
    with tempfile.TemporaryDirectory(dir=cache) as resolved:
        selection = Path(resolved)
        resolve = args[:]
        resolve[resolve.index('--dest')+1] = str(selection)
        resolve += ['--no-index', '--find-links', str(wheel_dir)]
        run(*resolve)
        dest = out / ('runtime/site-packages' if windows else 'vendor')
        for wheel in selection.glob('*.whl'):
            extract_zip(wheel, dest)
            # Wheel .data purelib/platlib roots must be installed at the import root.
        for data in dest.glob('*.data'):
            for library in ('purelib', 'platlib'):
                if (data / library).exists():
                    shutil.copytree(data / library, dest, dirs_exist_ok=True)
        dependencies = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in selection.glob('*.whl')}
        lock.write_text(json.dumps(dependencies,indent=2)+'\n')
        (out / 'dependencies.json').write_text(json.dumps(dependencies, indent=2)+'\n')


def toolchain_plan(boards, host):
    """Resolve every board's compiler profile for one host.

    One package serves every board, so --target must be able to pick the
    compiler on its own. That caps a backend at one profile: two wamrc builds
    for the same CPU could not be told apart at build time. Distinct profiles
    are otherwise welcome (today's Xtensa boards share one; a future RISC-V
    board adds its own) as long as they share one wasi-sdk version.
    """
    plan = {}
    owners = {}
    for board in boards:
        _, binding, profile, entry = select(ROOT, board, host)
        name = binding['profile']
        record = plan.setdefault(name, {'profile': profile, 'entry': entry, 'backends': {}})
        record['backends'].setdefault(binding['aot_backend'], []).append(board)
        if owners.setdefault(binding['aot_backend'], name) != name:
            raise ValueError(f'{binding["aot_backend"]}: profiles {owners[binding["aot_backend"]]} and {name} both back this CPU; a package cannot choose between them by --target')
        if len(record['backends']) > 1:
            raise ValueError(f'{name}: a compiler profile must serve exactly one AOT backend')
    wasi = {record['profile']['wasi_sdk'] for record in plan.values()}
    if len(wasi) != 1:
        raise ValueError('profiles in one package must share one wasi-sdk version: ' + json.dumps(sorted(wasi)))
    return plan


def stage_compilers(args, plan, host, cache):
    """Locate the shared wasi-sdk and one wamrc per backend, checking pinned digests."""
    windows = host.startswith('windows')
    wasi = args.windows_wasi if windows else args.linux_wasi
    override = args.windows_wamrc if windows else args.linux_wamrc
    if override and len(plan) > 1:
        raise ValueError('a --*-wamrc override pins one compiler; stage each profile locally instead')
    if windows and args.windows_toolchain_bundle and len(plan) > 1:
        raise ValueError('a pinned toolchain bundle serves one profile; stage each profile locally instead')
    if windows and args.windows_toolchain_bundle:
        # Clean release runners have no local Windows compiler cache. Validate
        # and load the supplied bundle before checking any source-tree paths.
        wasi, compiler = load_toolchain_bundle(args.windows_toolchain_bundle,
                                              cache/'selected-windows-toolchain', args.boards[0])
        backend, = next(iter(plan.values()))['backends']
        return wasi, {backend: compiler}
    wamrcs = {}
    for name, record in sorted(plan.items()):
        backend, = record['backends']
        entry = record['entry']
        wamrc = override or ROOT / entry['wamrc']
        if not wamrc.is_file():
            flag = '--windows-wamrc' if windows else '--linux-wamrc'
            raise ValueError(f'{wamrc}: AOT compiler for {backend} missing; stage the pinned profile or pass {flag}')
        check_wamrc(wamrc, entry['wamrc_sha256'])
        wamrcs[backend] = wamrc
        if wasi is None:
            wasi = ROOT / entry['wasi']
    return wasi, wamrcs


def windows_package(out, cache, args, plan):
    runtime = out / 'runtime'
    archive = args.windows_python_zip or cache / 'python-3.13.16-embed-amd64.zip'
    if not archive.is_file():
        print('Downloading official Windows embedded Python 3.13.16', flush=True)
        with urllib.request.urlopen('https://www.python.org/ftp/python/3.13.16/python-3.13.16-embed-amd64.zip', timeout=60) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest()!='97dae5274cc54867065e8d5a3226e48c35017ed332a0fdb0e27d5b5821961297':
        raise ValueError('Windows Python 3.13.16 embedded runtime checksum mismatch')
    extract_zip(archive, runtime)
    (runtime / 'python313._pth').write_text('python313.zip\n.\nsite-packages\n../tools\n../\nimport site\n')
    wheels(out, cache, True)
    if args.windows_toolchain_url:
        if not args.windows_toolchain_sha256:
            raise ValueError('remote compiler bundles require --windows-toolchain-sha256')
        archive=cache/'windows-toolchain.zip'
        if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest()!=args.windows_toolchain_sha256:
            with urllib.request.urlopen(args.windows_toolchain_url,timeout=120) as response:
                archive.write_bytes(response.read())
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=args.windows_toolchain_sha256:
            raise ValueError('remote compiler bundle checksum mismatch')
        args.windows_toolchain_bundle=archive
    wasi, wamrcs = stage_compilers(args, plan, 'windows-x64', cache)
    if not (wasi / 'bin/clang.exe').is_file():
        raise ValueError('Provide --windows-wasi DIR (or a compiler bundle); ABI updates reuse it unchanged')
    shutil.copytree(wasi, out / 'toolchain/wasi-sdk-24.0-x86_64-windows')
    for backend, compiler in sorted(wamrcs.items()):
        dest = out / ('toolchain/backends/'+backend if len(plan) > 1 else 'toolchain')
        dest.mkdir(parents=True, exist_ok=True)
        shutil.copy2(compiler, dest / 'wamrc.exe')
    for dll in wasi.glob('bin/*.dll'):
        shutil.copy2(dll, out / 'toolchain' / dll.name)
    compiler = shutil.which('x86_64-w64-mingw32-gcc')
    if not compiler:
        raise ValueError('Install gcc-mingw-w64-x86-64 on Ubuntu to cross-build the small EXE launchers')
    for name, script in [('aster','aster.py'),('aster-toolchain','windows_toolchain.py'),('aster-device','windows_device.py')]:
        run(compiler, '-Os', '-municode', '-static', '-s', '-DASTER_ENTRY=L"'+script+'"', ROOT / 'tools/windows_launcher.c', '-o', out / (name+'.exe'))
    (out / 'README.txt').write_text('Portable Windows x64 SDK. Keep the complete directory. Run aster.exe --version. No system Python or WSL required.\n')


def linux_package(out, cache, args, plan):
    wasi, wamrcs = stage_compilers(args, plan, 'linux-x64', cache)
    if not (wasi / 'bin/clang').is_file():
        raise ValueError('Provide the Linux wasi-sdk with --linux-wasi')
    # Copy only APP compiler/linker tools and the required Clang shared libraries.
    dest = out / 'toolchain/wasi-sdk-24.0-x86_64-linux'
    (dest / 'bin').mkdir(parents=True)
    (dest / 'lib').mkdir()
    for name in ('clang','wasm-ld','llvm-ar','llvm-nm'):
        shutil.copy2(wasi/'bin'/name, dest/'bin'/name)
        (dest/'bin'/name).chmod(0o755)
    shutil.copytree(wasi/'lib/clang', dest/'lib/clang')
    for library in wasi.glob('lib/*.so.*'):
        if not library.is_symlink(): shutil.copy2(library, dest/'lib'/library.name)
    for backend, compiler in sorted(wamrcs.items()):
        target = out / ('toolchain/backends/'+backend+'/wamrc' if len(plan) > 1 else 'toolchain/wamrc')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(compiler, target)
        target.chmod(0o755)
    wheels(out, cache, False)
    version = f'{sys.version_info.major}.{sys.version_info.minor}'
    for name, script in [('aster','aster.py'),('aster-toolchain','windows_toolchain.py'),('aster-device','windows_device.py')]:
        entry = out / name
        entry.write_text('#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexport PYTHONPATH="$root/vendor${PYTHONPATH:+:$PYTHONPATH}"\nexec python'+version+' "$root/tools/'+script+'" "$@"\n')
        entry.chmod(0o755)
    formats = {board: record['profile']['aot_format'] for record in plan.values() for boards in record['backends'].values() for board in boards}
    verify_linux(out, formats)
    (out / 'README.txt').write_text(f'Linux x86_64 SDK. Requires system Python {version}; Python dependencies and native toolchain are included. Run ./aster --version. Build on the oldest supported Ubuntu/glibc baseline.\n')


def package(platform, args):
    check_docs(ROOT)
    cache = args.cache.resolve(); cache.mkdir(parents=True, exist_ok=True)
    identity = source_identity(ROOT)
    plan = toolchain_plan(args.boards, platform+'-x64')
    with tempfile.TemporaryDirectory(prefix='sdk-'+platform+'-', dir=cache) as temporary:
        out = Path(temporary) / 'aster-sdk'; out.mkdir()
        copy_sdk(out, args.boards, platform)
        (windows_package if platform=='windows' else linux_package)(out, cache, args, plan)
        stage_development_kit(ROOT, out, platform+'-x64', cache, args.development_kit_dir)
        profiles = {name: {'aot_format': record['profile']['aot_format'],
                           'wasi_sdk': record['profile']['wasi_sdk'],
                           'backends': sorted(record['backends']),
                           'boards': sorted(b for boards in record['backends'].values() for b in boards)}
                    for name, record in sorted(plan.items())}
        (out / 'toolchain-profile.json').write_text(json.dumps({'profiles': profiles}, indent=2)+'\n')
        sums = []
        for path in sorted(out.rglob('*')):
            if path.is_file():
                sums.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.relative_to(out).as_posix())
        (out / 'SHA256SUMS.txt').write_text('\n'.join(sums)+'\n')
        if source_identity(ROOT)!=identity:
            raise ValueError('SDK source changed during packaging; rerun against a stable snapshot')
        args.output.mkdir(parents=True, exist_ok=True)
        name = f'aster-sdk-{identity["abi"]}-{platform}-x64-{identity["build_id"][:12]}'
        if platform=='windows':
            staging = shutil.make_archive(str(args.output / (name+'.partial')), 'zip', temporary, 'aster-sdk')
            result = str(args.output / (name+'.zip'))
            os.replace(staging,result)
        else:
            result = str(args.output / (name+'.tar.gz'))
            with tarfile.open(result+'.partial', 'w:gz') as archive:
                archive.add(out, arcname='aster-sdk')
            os.replace(result+'.partial',result)
        print('Packaged:', result, flush=True)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['windows','linux','all'], default='all')
    parser.add_argument('--hosts', help='Comma separated linux-x64,windows-x64 (overrides --platform)')
    parser.add_argument('--boards', help='Comma separated platform targets to package (default: every target in targets/)')
    parser.add_argument('--contract', type=Path, help='Import the validated firmware contract ZIP before packaging')
    parser.add_argument('--cache', type=Path, default=ROOT/'build/portable-cache')
    parser.add_argument('--output', type=Path, default=ROOT/'dist/portable')
    for name in ('windows-python-zip','windows-wasi','windows-wamrc','linux-wasi','linux-wamrc'):
        parser.add_argument('--'+name, type=Path)
    parser.add_argument('--development-kit-dir', type=Path, help='Local immutable kit payloads; defaults to pinned Release downloads')
    parser.add_argument('--windows-toolchain-bundle',type=Path)
    parser.add_argument('--windows-toolchain-url')
    parser.add_argument('--windows-toolchain-sha256')
    options=parser.parse_args()
    import platform
    if sys.platform!='linux' or platform.machine().lower() not in ('x86_64','amd64'):
        parser.error('Run the portable release builder on Ubuntu/Linux x86_64')
    if sys.version_info[:2]!=(3,12):
        parser.error('Use Python 3.12 for the supported Linux runtime and dependency wheel baseline')
    if options.contract:
        import_bundle(options.contract)
    hosts = [h.strip() for h in options.hosts.split(',')] if options.hosts else None
    if hosts and any(h not in ('linux-x64','windows-x64') for h in hosts):
        parser.error('supported hosts: linux-x64,windows-x64')
    platforms = [h.removesuffix('-x64') for h in hosts] if hosts else (['windows','linux'] if options.platform=='all' else [options.platform])
    options.boards = [b.strip() for b in options.boards.split(',')] if options.boards else sorted(p.stem for p in (ROOT/'targets').glob('*.json'))
    if not options.boards:
        parser.error('no boards to package: targets/ holds no target files')
    for target in platforms:
        toolchain_plan(options.boards, target+'-x64')
        package(target, options)
