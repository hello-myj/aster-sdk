import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_docs
import package_portable


class DocumentationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for folder in ('contract', 'targets', 'docs'):
            (self.root / folder).mkdir()
        (self.root / 'contract/lock.json').write_text(json.dumps({'abi': '3.7.0'}))
        (self.root / 'targets/xtensa.json').write_text('{}')

    def document(self, text):
        (self.root / 'docs/example.md').write_text(text)

    def test_current_docs_match_real_contract(self):
        check_docs.check()

    def test_cpu_commands_pass_but_board_targets_and_paths_fail(self):
        self.document('aster build APP --target xtensa\n当前 ABI 为 3.7.0。')
        check_docs.check(self.root)
        for text in ('aster build APP --target waveshare-35b',
                     'aster build APP --target=lckfb-s3',
                     'aster deploy dist/waveshare-35b/app.ast',
                     'aster init APP --board waveshare-35b',
                     '当前 ABI 为 3.4。',
                     '适用版本：SDK ABI **3.4.0**'):
            with self.subTest(text=text):
                self.document(text)
                with self.assertRaises(ValueError):
                    check_docs.check(self.root)

    def test_historical_abi_notes_are_allowed_and_archives_need_visible_redirect(self):
        self.document('ABI 3.4 introduced audio effects.\naster build APP --target TARGET')
        check_docs.check(self.root)
        self.document('<!-- aster-docs: historical -->\n历史说明，当前见 [发布指南](release.md)。\naster build APP --target waveshare-35b')
        check_docs.check(self.root)
        self.document('<!-- aster-docs: historical -->\nold notes')
        with self.assertRaises(ValueError):
            check_docs.check(self.root)

    def test_stale_docs_abort_packaging_before_cache_or_output_creation(self):
        self.document('aster build APP --target waveshare-35b')
        cache = self.root / 'cache'
        output = self.root / 'dist'
        with patch.object(package_portable, 'ROOT', self.root):
            with self.assertRaisesRegex(ValueError, 'unsupported --target'):
                package_portable.package('linux', SimpleNamespace(cache=cache, output=output))
        self.assertFalse(cache.exists())
        self.assertFalse(output.exists())
