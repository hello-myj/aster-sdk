#!/usr/bin/env python3
"""Recreate the original 2048 music and effects. Requires ffmpeg for MP3."""
import math
import subprocess
import tempfile
import wave
from pathlib import Path

RATE = 16000
OUT = Path(__file__).resolve().parent / "audio"


def wav(path, samples):
    with wave.open(str(path), "wb") as file:
        file.setnchannels(2)
        file.setsampwidth(2)
        file.setframerate(RATE)
        data = bytearray()
        for sample in samples:
            value = max(-32768, min(32767, round(sample * 32767)))
            data.extend(int(value).to_bytes(2, "little", signed=True) * 2)
        file.writeframes(data)


def tone(freq, t):
    return math.sin(2 * math.pi * freq * t) + 0.22 * math.sin(4 * math.pi * freq * t)


def effect(notes, duration, volume):
    count = round(duration * RATE)
    result = []
    for i in range(count):
        t = i / RATE
        step = min(len(notes) - 1, int(t * len(notes) / duration))
        local = t - step * duration / len(notes)
        envelope = min(1.0, local * 90) * math.exp(-3 * local / (duration / len(notes)))
        result.append(volume * envelope * tone(notes[step], t))
    return result


def music():
    # Eight calm, repeating bars. The first and last notes use the same chord.
    chords = [(261.63, 329.63, 392.00), (220.00, 261.63, 329.63),
              (174.61, 261.63, 349.23), (196.00, 293.66, 392.00)]
    duration = 16.0
    result = []
    for i in range(round(duration * RATE)):
        t = i / RATE
        bar = int(t / 4) % 4
        beat = int(t * 2) % 8
        chord = chords[bar]
        phase = t % 0.5
        pluck = math.exp(-6 * phase)
        pad = sum(math.sin(2 * math.pi * f * t) for f in chord) / 3
        note = tone(chord[(beat + bar) % 3] * 2, t)
        fade = min(1.0, t / 0.18, (duration - t) / 0.18)
        # Music stays below every action cue, regardless of the master volume.
        result.append(0.60 * fade * (0.075 * pad + 0.09 * pluck * note))
    return result


def main():
    OUT.mkdir(exist_ok=True)
    wav(OUT / "slide.wav", effect([370], 0.11, 0.29))
    wav(OUT / "merge.wav", effect([523, 659], 0.20, 0.35))
    wav(OUT / "win.wav", effect([523, 659, 784, 1047], 0.55, 0.32))
    wav(OUT / "over.wav", effect([392, 311, 247], 0.47, 0.31))
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp) / "bgm.wav"
        wav(source, music())
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                        "-i", str(source), "-codec:a", "libmp3lame", "-b:a", "48k",
                        str(OUT / "bgm.mp3")], check=True)


if __name__ == "__main__":
    main()
