"""Project scaffolding, shell activation and isolated output regression checks."""
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import aster
from sdk_install import repair_linux_links


class CLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='aster cli ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def invoke(self, *args):
        with contextlib.redirect_stdout(io.StringIO()):
            return aster.main(list(map(str, args)))

    def test_portable_install_reuses_runtime_and_activates_root(self):
        (self.root/'release.json').write_text('{}')
        (self.root/'templates').mkdir()
        with patch.object(aster,'ROOT',self.root), patch.object(aster,'executable',return_value=self.root/'tool.exe'), patch.object(aster.subprocess,'run') as run:
            self.assertEqual(self.invoke('install'),0)
            run.assert_not_called()
        env=(self.root/'env.ps1').read_text(encoding='utf-8-sig')
        self.assertIn(str(self.root),env)
        self.assertNotIn(str(self.root/'bin'),env)

    def test_scaffold_every_template_with_toml_escaped_name(self):
        for template in ('hello', 'swipe-game'):
            project = self.root / template
            self.invoke('init', project, '--template', template, '--name', '游戏 "A"\nB', '--id', 'test.' + template, '--target', 'xtensa')
            cfg = tomllib.loads((project / 'app.toml').read_text(encoding='utf-8'))
            self.assertEqual(cfg['app']['name'], '游戏 "A"\nB')
            self.assertEqual(cfg['sdk']['target'], 'xtensa')
            self.assertTrue(cfg['app']['aot'])
            for source in cfg['app'].get('sources', ['src/main.c']):
                self.assertTrue((project / source).is_file())
            for asset in cfg['app'].get('assets', []):
                self.assertTrue((project / asset).is_file())
            self.assertFalse((project / 'build.ps1').exists())

    def test_refuse_existing_project_without_overwriting(self):
        project = self.root / 'exists'
        project.mkdir()
        (project / 'keep').write_text('user data')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.invoke('init', project)
        self.assertEqual((project / 'keep').read_text(), 'user data')

    def test_invalid_id_and_board_leave_no_project(self):
        project = self.root / 'app'
        for args in [('--id', '../escape'), ('--target', 'unknown')]:
            with self.assertRaises(ValueError):
                self.invoke('init', project, *args)
            self.assertFalse(project.exists())

    def test_project_target_and_isolated_build_environment(self):
        project = self.root / 'app'
        self.invoke('init', project, '--id', 'test.app', '--target', 'xtensa')
        with patch.dict(os.environ, {'ASTER_TARGET': 'invalid'}), patch('aster.subprocess.run') as run, \
             patch('aster.tool_command', return_value=[sys.executable,str(aster.ROOT/'tools/build.py')]):
            self.invoke('build', project)
        args, options = run.call_args
        self.assertIn(str(project), args[0])
        self.assertEqual(options['cwd'], project)
        self.assertEqual(options['env']['ASTER_WORKSPACE'], str(project))
        self.assertIn('xtensa', args[0])

    def test_single_platform_is_the_default_target(self):
        project = self.root / 'app-default'
        with patch.dict(os.environ, {'ASTER_TARGET': ''}):
            self.invoke('init', project, '--id', 'test.board')
        cfg = tomllib.loads((project / 'app.toml').read_text())
        self.assertEqual(cfg['sdk']['target'], 'xtensa')
        with patch('aster.subprocess.run') as run:
            self.invoke('build', project)
        self.assertIn('xtensa', run.call_args.args[0])

    def test_run_no_build_deploys_existing_project_package(self):
        project = self.root / 'game'
        self.invoke('init', project, '--id', 'test.game', '--target', 'xtensa')
        package = project / 'dist/xtensa/test.game.ast'
        package.parent.mkdir(parents=True)
        package.write_bytes(b'existing package')
        with patch('aster.subprocess.call', return_value=0) as call:
            self.invoke('run', project, '--no-build', '--port', 'data', '--console', 'console')
        command = call.call_args.args[0]
        self.assertIn(str(package), command)
        self.assertIn('--run', command)
        self.assertIn('console', command)

    @unittest.skipIf(os.name == 'nt', 'POSIX shell roundtrip')
    def test_env_handles_spaces_and_apostrophes(self):
        sdk = self.root / "sdk's files"
        with patch('aster.home', return_value=sdk):
            script = aster.environment('bash')
        result = subprocess.check_output(['bash', '-c', script + 'printf "%s\\n%s" "$ASTER_SDK" "$PATH"'], text=True)
        self.assertEqual(result.splitlines()[0], str(sdk))
        self.assertTrue(result.splitlines()[1].startswith(str(sdk / 'bin') + ':'))

    @unittest.skipIf(os.name == 'nt', 'POSIX profile')
    def test_persist_is_idempotent_and_preserves_user_profile(self):
        profile = self.root / 'bashrc'
        profile.write_text('export USER_SETTING=keep\n')
        with patch('aster.home', return_value=self.root):
            with contextlib.redirect_stdout(io.StringIO()):
                aster.persist_path(profile)
                aster.persist_path(profile)
        text = profile.read_text()
        self.assertEqual(text.count('# >>> Aster SDK >>>'), 1)
        self.assertIn('export USER_SETTING=keep', text)

    @unittest.skipIf(os.name == 'nt', 'Linux symlink repair')
    def test_repair_links_stays_within_toolchain(self):
        tools = self.root / 'toolchain'
        tools.mkdir()
        (tools / 'real').write_bytes(b'\x7fELF executable')
        (tools / 'alias').write_text('real')
        (self.root / 'outside').write_text('user data')
        (tools / 'escape').write_text('../outside')
        repaired = repair_linux_links(tools)
        self.assertEqual(repaired, [tools / 'alias'])
        self.assertTrue((tools / 'alias').is_symlink())
        self.assertFalse((tools / 'escape').is_symlink())

if __name__ == '__main__':
    unittest.main()
