import hashlib
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import package_release

class Release(unittest.TestCase):
    def test_manual_skill_link_and_checksums_survive_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            binaries = root/'bin'
            binaries.mkdir()
            for name in package_release.EXES:
                (binaries/name).write_bytes(b'MZ-test')
            archive = package_release.assemble(package_release.ROOT, binaries, root/'output')
            with zipfile.ZipFile(archive) as zipped:
                files = {n: zipped.read(n) for n in zipped.namelist()}
            for name in ('docs/app-development.md', '.agents/skills/aster-app-development/SKILL.md'):
                self.assertEqual(files['aster-sdk/'+name], (package_release.ROOT/name).read_bytes())
            for line in files['aster-sdk/SHA256SUMS.txt'].decode().splitlines():
                digest, name = line.split('  ', 1)
                self.assertEqual(hashlib.sha256(files['aster-sdk/'+name]).hexdigest(), digest)
            self.assertNotIn('aster-sdk/apps/imucube/build', files)
            checksum = archive.with_suffix('.zip.sha256').read_text().split()[0]
            self.assertEqual(checksum, hashlib.sha256(archive.read_bytes()).hexdigest())

    def test_missing_device_tool_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(ValueError, 'Missing Windows executable'):
                package_release.assemble(package_release.ROOT, Path(folder), Path(folder)/'output')

if __name__ == '__main__': unittest.main()
