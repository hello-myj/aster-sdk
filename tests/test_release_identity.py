import json
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import release_identity as release


class ReleaseIdentityTests(unittest.TestCase):
    def test_build_id_uses_case_sensitive_relative_name_order(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'contract').mkdir();(root/'templates').mkdir()
            files={'contract/lock.json':b'{"abi":"3.7.0"}',
                   'templates/README.md':b'uppercase', 'templates/app.toml':b'lowercase'}
            for name,data in files.items():(root/name).write_bytes(data)
            for name in ('requirements.txt','requirements-windows.txt'):(root/name).write_bytes(b'')
            digest=hashlib.sha256()
            for name in sorted(files):
                digest.update(name.encode()+b'\0'+hashlib.sha256(files[name]).digest())
            for name in ('requirements.txt','requirements-windows.txt'):digest.update(name.encode())
            self.assertEqual(release.source_identity(root)['build_id'],digest.hexdigest())

    def test_interface_change_changes_build_id(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'contract').mkdir()
            (root / 'contract/lock.json').write_text('{"abi":"3.7.0"}')
            for name in ('requirements.txt', 'requirements-windows.txt'):
                (root / name).write_text('')
            before = release.source_identity(root)
            (root / 'contract/lock.json').write_text('{"abi":"3.8.0"}')
            after = release.source_identity(root)
            self.assertNotEqual(before['build_id'], after['build_id'])
            self.assertEqual(after['abi'], '3.8.0')

    def test_mixed_or_legacy_executable_rejected(self):
        expected = {'product':'aster','abi':'3.7.0','commit':'abc','build_id':'new'}
        with patch.object(release, 'identity', return_value=expected), patch.object(release.subprocess, 'run') as run:
            run.return_value.stdout = json.dumps({**expected, 'build_id':'old'})
            with self.assertRaisesRegex(RuntimeError, 'mixed SDK'):
                release.check_sibling(Path('aster-device.exe'))
            run.return_value.stdout = 'legacy help text'
            with self.assertRaisesRegex(RuntimeError, 'cannot read build identity'):
                release.check_sibling(Path('aster-device.exe'))
            run.return_value.stdout = json.dumps(expected)
            release.check_sibling(Path('aster-device.exe'))


if __name__ == '__main__':
    unittest.main()
