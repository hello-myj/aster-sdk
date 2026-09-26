"""Repair Linux archive links flattened by a Windows Git checkout."""
from pathlib import Path
import re


def repair_linux_links(root):
    root = Path(root).resolve()
    repaired = []
    if not root.is_dir():
        return repaired
    for path in root.rglob('*'):
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 200:
            continue
        try:
            value = path.read_text(encoding='ascii')
        except (UnicodeError, OSError):
            continue
        # Git symlink placeholders have exactly one relative filename, no newline.
        if not re.fullmatch(r'[A-Za-z0-9_./+-]+', value) or value.startswith('/'):
            continue
        target = path.parent / value
        if not target.exists() or not target.resolve().is_relative_to(root):
            continue
        path.unlink()
        try:
            path.symlink_to(value)
        except OSError:
            path.write_text(value, encoding='ascii')
            raise
        repaired.append(path)
    return repaired
