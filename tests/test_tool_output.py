import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build


class ToolOutput(unittest.TestCase):
    def test_native_ansi_diagnostics_survive_utf8_python(self):
        raw = '警告：中文路径'.encode('gbk')
        command = [sys.executable, '-c', f'import os; os.write(2, {raw!r})']
        with patch.object(build.locale, 'getencoding', return_value='gbk'):
            result = build.capture_tool(command)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, '警告：中文路径')

    def test_invalid_bytes_preserve_compiler_failure(self):
        command = [sys.executable, '-c', 'import os; os.write(2, b"bad \\xff diagnostic"); raise SystemExit(7)']
        output = io.StringIO()
        with patch.object(build.locale, 'getencoding', return_value='utf-8'):
            with contextlib.redirect_stderr(output), self.assertRaises(SystemExit) as error:
                build.run(command, 'compiler')
        self.assertEqual(error.exception.code, 1)
        self.assertIn('diagnostic', output.getvalue())
        self.assertIn('[FAIL] compiler', output.getvalue())
