"""Paths shared by source-tree tools and frozen Windows executables."""
import os
import sys
from pathlib import Path


def sdk_root() -> Path:
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))


def output_root() -> Path:
    value = os.environ.get("ASTER_WORKSPACE")
    return Path(value).resolve() if value else (
        Path.cwd() if getattr(sys, "frozen", False) else sdk_root())
