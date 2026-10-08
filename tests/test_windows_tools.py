"""Host checks for the Windows CLI's portable screenshot and path logic."""
import base64
import io
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import runtime_paths
import screenshot
import windows_device


class FakeSerial:
    def __init__(self, stream):
        self.stream = io.BytesIO(stream)
        self.written = bytearray()
        self.timeout = 0.2

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return None

    def write(self, data):
        self.written.extend(data)

    def flush(self):
        pass

    def reset_input_buffer(self):
        pass

    def read(self, count):
        return self.stream.read(count)


class WindowsTools(unittest.TestCase):
    def test_frozen_resource_and_workspace_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(sys, "_MEIPASS", directory, create=True), \
                 patch.object(sys, "frozen", True, create=True), \
                 patch.dict("os.environ", {"ASTER_WORKSPACE": directory}):
                self.assertEqual(runtime_paths.sdk_root(), Path(directory))
                self.assertEqual(runtime_paths.output_root(), Path(directory))

    def test_screenshot_stream_and_png(self):
        raw = bytes.fromhex("00f8e007")  # red, green RGB565LE
        payload_raw = zlib.compress(raw)
        payload = base64.b64encode(payload_raw)
        stream = (b"console noise\nSHOT hdr 2x1 zlib " + str(len(payload_raw)).encode() + b"\n"
                  b"SHOT b64 0000 " + payload + b"\nSHOT end " +
                  f"{zlib.crc32(payload_raw):08x}".encode() + b"\n")
        fake = FakeSerial(stream)
        with patch.object(windows_device.aster_send, "open_serial", return_value=fake), \
             patch.object(screenshot.time, "sleep"):
            width, height, captured = screenshot.capture("COM5")
        self.assertEqual((width, height, captured), (2, 1, raw))
        self.assertIn(b"aster ui screenshot", fake.written)
        png = screenshot.png_bytes(width, height, captured)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        while offset < len(png):
            size = struct.unpack_from(">I", png, offset)[0]
            kind = png[offset + 4:offset + 8]
            chunk = png[offset + 8:offset + 8 + size]
            if kind == b"IDAT":
                self.assertEqual(zlib.decompress(chunk), bytes([0, 255, 0, 0, 0, 255, 0]))
            offset += 12 + size

    def test_screenshot_rejects_bad_crc(self):
        stream = b"SHOT hdr 1x1 raw 2\nSHOT b64 0000 AAA=\nSHOT end deadbeef\n"
        fake = FakeSerial(stream)
        with patch.object(windows_device.aster_send, "open_serial", return_value=fake), \
             patch.object(screenshot.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "CRC"):
                screenshot.capture("COM5")

    def test_flash_passes_explicit_port_and_offset(self):
        commands = []
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "merged.bin"
            image.write_bytes(b"firmware")
            with patch.dict(sys.modules, {"esptool": SimpleNamespace(main=commands.append)}):
                self.assertEqual(windows_device.main(["flash", str(image),
                    "--port", "COM7", "--address", "0x0"]), 0)
        self.assertEqual(commands[0][-2:], ["0x0", str(image)])
        self.assertIn("COM7", commands[0])

    def test_device_build_requests_aot(self):
        with patch.object(windows_device, "toolchain_path", return_value=Path("tool.exe")), \
             patch.object(windows_device.subprocess, "run") as run:
            self.assertEqual(windows_device.main(["build", str(ROOT / "apps/hello")]), 0)
        self.assertEqual(run.call_args.args[0][-1], "--aot")


if __name__ == "__main__":
    unittest.main()
