"""Compile the app's pure chess rules for host-side rule verification."""
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CHESS = ROOT / "apps/chess"


class ChessRules(unittest.TestCase):
    def test_rules_and_perft(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "chess-rules"
            subprocess.run(
                ["cc", "-O2", "-Wall", "-Wextra", "-I", str(CHESS / "src"),
                 str(CHESS / "src/chess.c"), str(CHESS / "tests/rules.c"),
                 "-o", str(binary)], check=True,
            )
            output = subprocess.check_output([str(binary)], text=True)
            self.assertIn("chess rules OK", output)
