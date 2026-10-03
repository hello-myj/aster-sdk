import json
import sys
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build import App, make_manifest


class Description(unittest.TestCase):
    def app(self, directory, description):
        path = Path(directory)
        (path / 'src').mkdir(exist_ok=True)
        (path / 'src/main.c').write_text('int main(void) {return 0;}')
        field = '' if description is None else 'description = ' + json.dumps(description, ensure_ascii=False) + '\n'
        (path / 'app.toml').write_text('[app]\nid="test.description"\nname="测试"\n' + field)
        app = App(path)
        app.target = {'id': 'waveshare-35b'}
        return app

    def test_optional_metadata_and_unicode_quotes(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertNotIn('description', make_manifest(self.app(d, None)))
            self.assertNotIn('description', make_manifest(self.app(d, '')))
            text = '播放音乐："收藏"，路径 C:\\Music'
            self.assertEqual(make_manifest(self.app(d, text))['description'], text)
            self.assertEqual(make_manifest(self.app(d, 'a' * 124 + '乐'))['description'], 'a' * 124 + '乐')

    def test_description_survives_real_build_and_package(self):
        from verify_ast import check
        with tempfile.TemporaryDirectory() as d:
            text = '动态加载：音乐与阅读'
            app = self.app(d, text)
            (app.dir / 'src/main.c').write_text('int aster_app_init(void) {return 0;}')
            subprocess.run([sys.executable, str(ROOT / 'tools/build.py'), d,
                            '--target', 'waveshare-35b', '--no-aot'],
                           check=True, capture_output=True, text=True)
            package = ROOT / 'dist/waveshare-35b/test.description.ast'
            self.assertEqual(check(package, board='waveshare-35b')['description'], text)

    def test_rejects_oversize_and_non_text(self):
        with tempfile.TemporaryDirectory() as d:
            for value in ('a' * 128, '乐' * 43, '第一行\n第二行', 42):
                with self.subTest(value=value), self.assertRaises(SystemExit):
                    self.app(d, value)
