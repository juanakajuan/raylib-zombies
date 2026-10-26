#!/usr/bin/env python3
import array
import runpy
import sys
import wave
from pathlib import Path

# run_path compiles the source each time. An import can reuse a stale .pyc when an edit keeps the size and the mtime second.
CLIPS = runpy.run_path(str(Path(__file__).resolve().parent / "sound_levels.py"))["CLIPS"]

TOLERANCE = 0.005


def peak(path: Path) -> float:
    with wave.open(str(path), "rb") as clip:
        if clip.getsampwidth() != 2:
            raise ValueError(f"{path.name} is not 16-bit")
        samples = array.array("h", clip.readframes(clip.getnframes()))
    return max(map(abs, samples), default=0) / 32767


def main() -> int:
    assets = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "assets"
    files = {path.stem: path for path in assets.glob("*.wav")}
    failed = False
    for name in sorted(CLIPS.keys() | files.keys()):
        if name not in files:
            print(f"{name} missing from {assets}")
            failed = True
        elif name not in CLIPS:
            print(f"{name} missing from the CLIPS table")
            failed = True
        else:
            measured = peak(files[name])
            expected = CLIPS[name].finished_peak
            if abs(measured - expected) <= TOLERANCE:
                print(f"{name} peak {measured:.3f} ok")
            else:
                print(f"{name} peak {measured:.3f} expected {expected:.3f} FAIL")
                failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
