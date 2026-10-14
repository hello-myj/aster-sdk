#!/usr/bin/env python3
"""Cross-platform Aster SDK: install, env, init, build and run."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tomllib

from runtime_paths import sdk_root
from release_identity import version_requested, check_sibling

ROOT = sdk_root()


def packaged():
    return getattr(sys, 'frozen', False) or (ROOT/'release.json').is_file()


def home():
    # Frozen resources are temporary; installation and PATH must use real files.
    if getattr(sys, 'frozen', False):
        return Path(sys.executable).resolve().parent
    return ROOT


def executable(name):
    candidates = [home() / (name + '.exe'), ROOT / 'dist/windows' / (name + '.exe')]
    return next((p for p in candidates if p.is_file()), None)


def tool_command(kind):
    if os.name == 'nt':
        binary = executable('aster-toolchain' if kind == 'build' else 'aster-device')
        if binary:
            try:
                check_sibling(binary)
            except RuntimeError:
                if getattr(sys, 'frozen', False) or (ROOT / 'release.json').is_file():
                    raise
                # An updated source checkout must not silently use an old frozen contract.
            else:
                return [str(binary)]
        if getattr(sys, 'frozen', False):
            raise ValueError('Keep aster.exe, aster-toolchain.exe and aster-device.exe together')
    script = 'build.py' if kind == 'build' else 'windows_device.py'
    return [sys.executable, str(ROOT / 'tools' / script)]


def targets():
    return sorted(p.stem for p in (ROOT / 'targets').glob('*.json'))


def target_for(explicit=None, project=None):
    selected = explicit
    if not selected and project:
        config = tomllib.loads((project / 'app.toml').read_text(encoding='utf-8'))
        selected = config.get('sdk', {}).get('target')
    selected = selected or os.environ.get('ASTER_TARGET')
    known = targets()
    if not selected and len(known) == 1:
        selected = known[0]
    if selected not in known:
        raise ValueError('Select --target from: ' + ', '.join(known))
    return selected


def project_path(value):
    path = Path(value).resolve()
    if not (path / 'app.toml').is_file():
        raise ValueError(f'No app.toml in {path}; use aster init <directory> first')
    return path


def create(args):
    destination = args.directory.resolve()
    identifier = args.id or 'app.' + re.sub(r'[^A-Za-z0-9_-]', '-', destination.name).strip('-')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,62}', identifier) or '..' in identifier:
        raise ValueError('--id must be a safe app identifier, up to 63 characters')
    if destination.exists():
        raise ValueError('destination already exists')
    template = ROOT / 'templates' / args.template
    if not template.is_dir():
        raise ValueError('Unknown template; use aster templates')
    target = target_for(args.target)
    manifest = (template / 'app.toml').read_text(encoding='utf-8')
    # Replace full TOML fields, preserving template metadata and assets.
    manifest = re.sub(r'^id = .*$', 'id = ' + json.dumps(identifier), manifest, flags=re.M)
    name = json.dumps(args.name or destination.name, ensure_ascii=False)
    manifest = re.sub(r'^name = .*$', lambda _: 'name = ' + name, manifest, flags=re.M)
    manifest += '\n[sdk]\ntarget = ' + json.dumps(target) + '\n'
    tomllib.loads(manifest)
    shutil.copytree(template, destination,
                    ignore=shutil.ignore_patterns('dist', 'build', '__pycache__', '*.pyc'))
    (destination / 'app.toml').write_text(manifest, encoding='utf-8')
    print(destination)
    print(f'Next: cd {shlex.quote(str(destination))}; aster build; aster run')
    return 0


def environment(shell):
    base = home()
    folders = [base] if packaged() else [base / 'bin']
    if not packaged():
        pybin = base / ('.venv-windows/Scripts' if os.name == 'nt' else '.venv-linux/bin')
        if pybin.is_dir():
            folders.append(pybin)
    if shell in ('bash', 'zsh', 'sh'):
        return 'export ASTER_SDK=' + shlex.quote(str(base)) + '\nexport PATH=' + shlex.quote(':'.join(map(str, folders))) + ':"$PATH"\n'
    literal = lambda s: "'" + str(s).replace("'", "''") + "'"
    if shell == 'powershell':
        return '$env:ASTER_SDK = ' + literal(base) + '\n$env:PATH = ' + literal(';'.join(map(str, folders))) + ' + ";" + $env:PATH\n'
    return '@chcp 65001 >nul\nset "ASTER_SDK=' + str(base) + '"\nset "PATH=' + ';'.join(map(str, folders)) + ';%PATH%"\n'


def persist_path(profile=None):
    if os.name == 'nt':
        import winreg
        folder = str(home() if packaged() else home() / 'bin')
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, 'Environment') as key:
            try:
                value, kind = winreg.QueryValueEx(key, 'Path')
            except FileNotFoundError:
                value, kind = '', winreg.REG_EXPAND_SZ
            entries = [p for p in value.split(';') if p]
            if folder.casefold().rstrip('\\') not in [p.casefold().rstrip('\\') for p in entries]:
                winreg.SetValueEx(key, 'Path', 0, kind, ';'.join([folder, *entries]))
        # Inform Explorer so terminals launched later inherit the new user PATH.
        import ctypes
        result = ctypes.c_size_t()
        ctypes.windll.user32.SendMessageTimeoutW(0xFFFF, 0x001A, 0, ctypes.c_wchar_p('Environment'), 2, 5000, ctypes.byref(result))
        print('User PATH configured. Open a new terminal, or source env.ps1 now.')
    else:
        path = Path(profile).expanduser() if profile else Path.home() / ('.zshrc' if os.environ.get('SHELL', '').endswith('zsh') else '.bashrc')
        marker = '# >>> Aster SDK >>>'
        block = marker + '\n. ' + shlex.quote(str(home() / 'env.sh')) + '\n# <<< Aster SDK <<<'
        text = path.read_text(encoding='utf-8') if path.exists() else ''
        text = re.sub(r'(?m)^# >>> Aster SDK >>>\n.*?^# <<< Aster SDK <<<', lambda _: block, text, flags=re.S) if marker in text else text.rstrip('\n') + '\n\n' + block + '\n'
        path.write_text(text, encoding='utf-8')
        print(f'PATH configured in {path}. Source env.sh to activate this terminal.')


def install(args):
    if os.name == 'posix' and not packaged():
        from sdk_install import repair_linux_links
        for path in repair_linux_links(ROOT / 'toolchain'):
            print('Restored Linux symlink: ' + str(path))
    if not packaged():
        venv = home() / ('.venv-windows' if os.name == 'nt' else '.venv-linux')
        python = venv / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
        if not python.is_file():
            subprocess.run([sys.executable, '-m', 'venv', str(venv)], check=True)
        subprocess.run([str(python), '-m', 'pip', 'install', '-r', str(ROOT / 'requirements.txt')], check=True)
        if args.flash:
            subprocess.run([str(python), '-m', 'pip', 'install', 'esptool>=5,<6'], check=True)
    elif os.name=='nt' and (not executable('aster-toolchain') or not executable('aster-device')):
        raise ValueError('Installation requires all three Windows executables in one directory')
    for filename, shell in [('env.sh', 'bash'), ('env.ps1', 'powershell'), ('env.cmd', 'cmd')]:
        (home() / filename).write_text(environment(shell), encoding='utf-8-sig' if shell == 'powershell' else 'utf-8')
    if args.add_path:
        persist_path(args.profile)
    print('SDK ready: ' + str(home()))
    print('Activate: ' + ('. "' + str(home() / 'env.ps1') + '"' if os.name == 'nt' else 'source ' + shlex.quote(str(home() / 'env.sh'))))
    return 0


def build_project(args):
    project = ROOT if args.all else project_path(args.project)
    target = target_for(args.target, None if args.all else project)
    command = tool_command('build')
    if os.name == 'nt' and command[0].endswith('.exe'):
        command.append('build')
    command += ['--target', target]
    command += ['--all'] if args.all else [str(project)]
    for option in ('aot', 'no_aot', 'clean'):
        if getattr(args, option, False):
            command.append('--' + option.replace('_', '-'))
    env = dict(os.environ, ASTER_WORKSPACE=str(project))
    subprocess.run(command, cwd=project, env=env, check=True)
    return project, target


def doctor():
    good = True
    if os.name == 'nt' and (getattr(sys, 'frozen', False) or executable('aster-toolchain')):
        for name in ('aster-toolchain', 'aster-device'):
            path = executable(name)
            print(f'{name}: {path or "MISSING"}')
            good &= bool(path)
    else:
        print(f'Python: {sys.version.split()[0]} ({sys.executable})')
        for module in ('serial', 'cryptography'):
            found = importlib.util.find_spec(module) is not None
            print(f'{module}: {"OK" if found else "MISSING; run aster install"}')
            good &= found
        # Reuse the builder search order (including ASTER_WASI_SDK/ASTER_WAMRC).
        import build
        clang = build.find_clang()
        found = bool(clang) and Path(clang).is_file()
        print(f'clang: {clang if found else "MISSING"}')
        good &= found
        if found:
            try:
                subprocess.run([str(clang), '--version'], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
            except (OSError, subprocess.CalledProcessError) as error:
                print(f'clang cannot execute: {error}; run aster install (repairs Linux symlinks)')
                good = False
        # One compiler per AOT backend in multi-CPU packages; a single shared
        # compiler otherwise (the flat toolchain/ layout every SDK used before).
        wamrcs = build.bundled_wamrcs()
        for backend, path in wamrcs.items():
            print(f'wamrc[{backend}]: {path}')
        if not wamrcs:
            wamrc = build.find_wamrc()
            found = bool(wamrc) and Path(wamrc).is_file()
            print(f'wamrc: {wamrc if found else "MISSING"}')
            good &= found
    print('Targets: ' + ', '.join(targets()))
    return 0 if good else 1


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if version_requested(argv):
        return 0
    device_commands = {'ports', 'probe', 'push', 'shell', 'debug', 'screenshot', 'flash'}
    if argv and argv[0] in device_commands:
        return subprocess.call(tool_command('device') + argv)
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('targets', 'templates', 'doctor'):
        commands.add_parser(name)
    setup = commands.add_parser('install', help='Install isolated Python dependencies and environment scripts')
    setup.add_argument('--add-path', action='store_true', help='Persist PATH for current user')
    setup.add_argument('--profile', help='Linux shell profile to update instead of ~/.bashrc or ~/.zshrc')
    setup.add_argument('--flash', action='store_true', help='Also install esptool (source installation only)')
    env = commands.add_parser('env', aliases=['source'], help='Print environment commands for the calling shell')
    env.add_argument('--shell', choices=['bash', 'zsh', 'sh', 'powershell', 'cmd'], default='powershell' if os.name == 'nt' else 'bash')
    new = commands.add_parser('init', aliases=['new'], help='Create an independent project from a template')
    new.add_argument('directory', type=Path)
    new.add_argument('--id')
    new.add_argument('--name')
    new.add_argument('--template', default='hello', choices=sorted(p.name for p in (ROOT / 'templates').iterdir() if (p / 'app.toml').is_file()))
    new.add_argument('--target')
    for name in ('build', 'run'):
        cmd = commands.add_parser(name)
        cmd.add_argument('project', nargs='?', default='.')
        cmd.add_argument('--target')
        aot = cmd.add_mutually_exclusive_group()
        aot.add_argument('--aot', action='store_true')
        aot.add_argument('--no-aot', action='store_true')
        cmd.add_argument('--all', action='store_true', help='Build SDK examples (build only)')
        cmd.add_argument('--clean', action='store_true', help='Delete project build/ and dist/ (build only)')
        if name == 'run':
            cmd.add_argument('--port', default='auto')
            cmd.add_argument('--console', default='auto')
            cmd.add_argument('--no-build', action='store_true')
    for name in ('deploy', 'verify'):
        cmd = commands.add_parser(name)
        cmd.add_argument('package', type=Path)
        cmd.add_argument('--target')
        if name == 'deploy':
            cmd.add_argument('--port', default='auto')
            cmd.add_argument('--console', default='auto')
            cmd.add_argument('--run', action='store_true')
        else:
            cmd.add_argument('--pubkey', type=Path)
    # Visible discovery; device parser owns their arguments.
    for name in sorted(device_commands):
        commands.add_parser(name, add_help=False, help='Device tools; use ' + name + ' --help')
    args = parser.parse_args(argv)
    if args.command in ('env', 'source'):
        print(environment(args.shell), end='')
        return 0
    if args.command == 'install':
        return install(args)
    if args.command in ('init', 'new'):
        return create(args)
    if args.command == 'templates':
        for path in sorted((ROOT / 'templates').iterdir()):
            if (path / 'app.toml').is_file():
                print(path.name)
        return 0
    if args.command == 'targets':
        for name in targets():
            print(name)
        return 0
    if args.command == 'doctor':
        return doctor()
    if args.command in ('build', 'run'):
        if args.command == 'run' and (args.all or args.clean):
            parser.error('run does not accept --all or --clean')
        if args.command == 'run' and args.no_build:
            project = project_path(args.project)
            target = target_for(args.target, project)
        else:
            project, target = build_project(args)
        if args.command == 'build':
            return 0
        manifest = tomllib.loads((project / 'app.toml').read_text(encoding='utf-8'))
        args.package = project / 'dist' / target / (manifest['app']['id'] + '.ast')
        args.run = True
    else:
        target = target_for(args.target)
    package = args.package.resolve()
    if not package.is_file():
        raise ValueError(f'Package missing: {package}; run aster build first')
    if args.command == 'verify':
        binary = executable('aster-toolchain') if os.name == 'nt' else None
        command = [str(binary), 'verify'] if binary else [sys.executable, str(ROOT / 'tools/verify_ast.py')]
        command += ['--target', target]
        if args.pubkey:
            command += ['--pubkey', str(args.pubkey)]
        command.append(str(package))
    else:
        command = tool_command('device') + ['deploy', str(package), '--target', target, '--port', args.port, '--console', args.console]
        if args.run:
            command.append('--run')
    return subprocess.call(command)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
