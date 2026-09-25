#!/usr/bin/env python3
"""Generate the original 16 kHz stereo PCM sound effects used by Snake."""
import math
import wave
from pathlib import Path

RATE = 16000
OUT = Path(__file__).resolve().parent / "audio"


def cue(notes, seconds, gain, decay):
    samples = []
    step_length = seconds / len(notes)
    for i in range(round(seconds * RATE)):
        t = i / RATE
        step = min(len(notes) - 1, int(t / step_length))
        local = t - step * step_length
        envelope = min(1.0, local * 120) * math.exp(-decay * local / step_length)
        frequency = notes[step]
        wave_value = math.sin(2 * math.pi * frequency * t)
        wave_value += 0.18 * math.sin(4 * math.pi * frequency * t)
        samples.append(gain * envelope * wave_value)
    return samples


def write(name, samples):
    with wave.open(str(OUT / name), "wb") as file:
        file.setnchannels(2)
        file.setsampwidth(2)
        file.setframerate(RATE)
        data = bytearray()
        for sample in samples:
            value = max(-32768, min(32767, round(sample * 32767)))
            data.extend(value.to_bytes(2, "little", signed=True) * 2)
        file.writeframes(data)


def main():
    OUT.mkdir(exist_ok=True)
    write("eat.wav", cue([660, 880], 0.16, 0.38, 3.2))
    write("death.wav", cue([330, 247, 165], 0.42, 0.36, 2.6))
    write("restart.wav", cue([440, 660], 0.20, 0.32, 3.0))
    # The reference board's small speaker attenuates the old 220 Hz thump.
    # A 600 Hz, 70 ms cue remains distinct at the game's 8 steps per second.
    write("move.wav", cue([600], 0.070, 0.35, 2.6))


if __name__ == "__main__":
    main()
