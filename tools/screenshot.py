"""Capture Aster's CRC-protected RGB565 screenshot stream as PNG."""
import base64
import re
import struct
import time
import zlib
from pathlib import Path


HEADER = re.compile(rb"SHOT hdr (\d+)x(\d+) (raw|zlib) (\d+)")
CHUNK = re.compile(rb"SHOT b64 (\d+) ([A-Za-z0-9+/=]+)")
END = re.compile(rb"SHOT end ([0-9a-fA-F]{8})")


def capture(port: str, page: str | None = None, timeout: float = 45) -> tuple[int, int, bytes]:
    from aster_send import open_serial

    command = "aster ui screenshot" + (" " + page if page else "")
    with open_serial(port, timeout=0.2, write_timeout=5) as connection:
        connection.write(b"\x03\r\n")
        time.sleep(0.2)
        connection.reset_input_buffer()
        connection.write((command + "\r\n").encode("ascii"))
        connection.flush()
        width = height = length = None
        codec = None
        chunks: dict[int, bytes] = {}
        crc = None
        line_buffer = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line_buffer.extend(connection.read(4096))
            if len(line_buffer) > 8192:
                raise RuntimeError("screenshot line too long or serial data corrupted")
            while b"\n" in line_buffer:
                line, _, rest = line_buffer.partition(b"\n")
                line_buffer = bytearray(rest)
                line = line.strip()
                if b"SHOT err" in line or b"screenshot:" in line:
                    raise RuntimeError(line.decode(errors="replace"))
                match = HEADER.search(line)
                if match:
                    width, height = int(match[1]), int(match[2])
                    codec = match[3]
                    length = int(match[4])
                    raw_len = width * height * 2
                    if not (0 < width <= 1024 and 0 < height <= 1024 and 0 < length):
                        raise RuntimeError("invalid screenshot dimensions")
                    if codec == b"raw" and length != raw_len:
                        raise RuntimeError("invalid screenshot dimensions")
                    if codec == b"zlib" and length > raw_len + 64:
                        raise RuntimeError("invalid screenshot dimensions")
                    chunks.clear()
                    continue
                match = CHUNK.search(line)
                if match and length is not None:
                    index = int(match[1])
                    if index >= (length + 479) // 480:
                        raise RuntimeError("invalid screenshot chunk index")
                    try:
                        part = base64.b64decode(match[2], validate=True)
                    except ValueError:
                        continue
                    if len(part) == min(480, length - index * 480):
                        chunks[index] = part
                    continue
                match = END.search(line)
                if match:
                    crc = int(match[1], 16)
                    break
            if crc is not None:
                break
        if crc is None or length is None:
            raise TimeoutError("screenshot did not complete")
        count = (length + 479) // 480
        if len(chunks) != count or any(i not in chunks for i in range(count)):
            raise RuntimeError(f"missing screenshot chunks: {len(chunks)}/{count}")
        payload = b"".join(chunks[i] for i in range(count))
        if len(payload) != length or zlib.crc32(payload) != crc:
            raise RuntimeError("screenshot length or CRC mismatch")
        raw = zlib.decompress(payload) if codec == b"zlib" else payload
        if len(raw) != width * height * 2:
            raise RuntimeError("screenshot inflated size mismatch")
        return width, height, raw


def png_bytes(width: int, height: int, rgb565: bytes) -> bytes:
    if len(rgb565) != width * height * 2:
        raise ValueError("RGB565 size does not match dimensions")
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            pos = (y * width + x) * 2
            value = rgb565[pos] | (rgb565[pos + 1] << 8)
            r = (value >> 11) & 31
            g = (value >> 5) & 63
            b = value & 31
            rows.extend((r * 255 // 31, g * 255 // 63, b * 255 // 31))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data)))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def save_png(port: str, output: Path, page: str | None = None) -> tuple[int, int]:
    last_error = None
    for attempt in range(3):
        try:
            width, height, raw = capture(port, page)
            break
        except (RuntimeError, TimeoutError) as error:
            last_error = error
    else:
        raise RuntimeError(f"screenshot failed after 3 attempts: {last_error}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(png_bytes(width, height, raw))
    return width, height
