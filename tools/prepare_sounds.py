#!/usr/bin/env python3
"""Cut the downloaded source sounds into the mono 16-bit clips in assets/.

Usage: tools/prepare_sounds.py <dir with the extracted source files> [output dir]
"""
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 44100
PEAK = 0.9
PRE_ROLL = 0.01
FADE = 0.01
ONSET_FRACTION = 0.1

# output name: (source file, seconds to keep from the first loud sample, or None for the whole file)
CLIPS = {
    "fire_pistol": ("cz.wav", 0.6),
    "fire_smg": ("sks.wav", 0.4),
    "fire_shotgun": ("shotty.wav", None),
    "reload_pistol": ("gunreload1.wav", None),
    "reload_smg": ("assaultriflereload1_0.wav", None),
    "reload_shotgun": ("shotguncock_0.wav", None),
    "growl_1": ("zombie-17.wav", None),
    "growl_2": ("zombie-16.wav", None),
    "growl_3": ("zombie-21.wav", None),
    "attack_1": ("zombie-24.wav", None),
    "attack_2": ("zombie-5.wav", None),
    "attack_3": ("zombie-11.wav", None),
}


def decode(path: Path) -> np.ndarray:
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"],
        check=True, capture_output=True,
    ).stdout
    return np.frombuffer(raw, dtype="<f4").copy()


def cut(samples: np.ndarray, seconds: float | None) -> np.ndarray:
    if seconds is None:
        return samples
    loud = np.flatnonzero(np.abs(samples) >= ONSET_FRACTION * np.abs(samples).max())[0]
    start = max(0, loud - int(PRE_ROLL * RATE))
    return samples[start:start + int(seconds * RATE)]


def finish(samples: np.ndarray) -> np.ndarray:
    samples = samples / np.abs(samples).max() * PEAK
    fade = min(int(FADE * RATE), len(samples))
    samples[-fade:] *= np.linspace(1.0, 0.0, fade)
    return samples


def write(path: Path, samples: np.ndarray) -> None:
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes((samples * 32767).astype("<i2").tobytes())


def main() -> None:
    source = Path(sys.argv[1])
    dest = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "assets"
    dest.mkdir(parents=True, exist_ok=True)
    for name, (filename, seconds) in CLIPS.items():
        clip = finish(cut(decode(source / filename), seconds))
        write(dest / f"{name}.wav", clip)
        print(f"{name}.wav {len(clip) / RATE:.2f}s")


if __name__ == "__main__":
    main()
