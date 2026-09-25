#!/usr/bin/env python3
"""Generate the original short chess-piece click (16 kHz stereo PCM WAV)."""
import math
import wave
from pathlib import Path

RATE = 16000
DURATION = 0.105
OUT = Path(__file__).resolve().parent / "audio" / "move.wav"


def main():
    OUT.parent.mkdir(exist_ok=True)
    data = bytearray()
    noise_state = 0x53434853
    for i in range(round(RATE * DURATION)):
        t = i / RATE
        noise_state = (1664525 * noise_state + 1013904223) & 0xFFFFFFFF
        noise = ((noise_state >> 16) / 32768.0) - 1.0
        attack = min(1.0, t * 1600)
        body = (math.sin(2 * math.pi * 690 * t) * 0.65 +
                math.sin(2 * math.pi * 1210 * t) * 0.25)
        click = noise * math.exp(-105 * t) * 0.23
        sample = attack * (body * math.exp(-34 * t) * 0.72 + click)
        value = max(-32768, min(32767, round(sample * 32767)))
        data.extend(value.to_bytes(2, "little", signed=True) * 2)
    with wave.open(str(OUT), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(data)


if __name__ == "__main__":
    main()
