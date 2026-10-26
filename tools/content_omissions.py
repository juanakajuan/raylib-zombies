#!/usr/bin/env python3
"""Prove that a weapon or a perk cannot be added by half.

Copies the working tree to a temporary folder, adds a dummy weapon row and a dummy perk row to
src/content.h, and builds and tests that. Then it removes one column at a time from each new row and
expects the build to fail. A partial row that builds is a silent omission. Exit status is 1 if there
is one or more silent omissions.

Usage: python3 -I tools/content_omissions.py [--keep]
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONTENT = pathlib.Path("src/content.h")

WEAPON_COLUMNS = [
    ("id", "RIFLE"), ("name", '"Rifle"'), ("damage", "70.0f"), ("fire_interval", "0.5f"),
    ("automatic", "false"), ("mag_size", "10"), ("reserve_max", "60"), ("reload_time", "2.0f"),
    ("pellets", "1"), ("spread", "0.0f"), ("wall_cost", "2000"), ("view_length", "0.26f"),
    ("panel", "CONTENT_RGB(60, 140, 80)"),
    ("fire_sound", 'CONTENT_SOUND(0.6f, 1.0f, FireShotgun, "fire_shotgun.wav")'),
    ("reload_sound", 'CONTENT_SOUND(0.95f, 0.6f, Reload, "reload_shotgun.wav")'),
]
PERK_COLUMNS = [
    ("id", "DUMMY"), ("name", '"Dummy Cola"'), ("cost", "2000"), ("max_health", "0.0f"),
    ("reload_scale", "0.75f"), ("body", "CONTENT_RGB(140, 40, 160)"), ("glow", "CONTENT_RGB(255, 150, 255)"),
]


def row(columns, drop=None):
    return "X(" + ", ".join(value for name, value in columns if name != drop) + ")"


def append_row(text, list_name, new_row):
    lines = text.split("\n")
    start = lines.index(f"#define {list_name}(X) \\")
    end = start
    while lines[end].endswith("\\"):
        end += 1
    lines[end] += " \\"
    lines.insert(end + 1, "    " + new_row)
    return "\n".join(lines)


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def first_error(output):
    for line in output.splitlines():
        if "error" in line:
            return line.split("/src/", 1)[-1].strip()[:140]
    return "failed"


def check(work, original, weapon_row, perk_row, with_tests):
    text = append_row(append_row(original, "WEAPON_LIST", weapon_row), "PERK_LIST", perk_row)
    (work / CONTENT).write_text(text)
    build = run(["cmake", "--build", "build", "--parallel"], work)
    if build.returncode != 0:
        return "build fails", first_error(build.stdout + build.stderr)
    if not with_tests:
        return "builds", ""
    test = run(["ctest", "--test-dir", "build", "--output-on-failure"], work)
    if test.returncode != 0:
        return "tests fail", first_error(test.stdout)
    return "builds, tests pass", ""


def main():
    keep = "--keep" in sys.argv[1:]
    files = run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], ROOT).stdout.split("\0")
    work = pathlib.Path(tempfile.mkdtemp(prefix="content-omissions-"))
    try:
        for name in filter(None, files):
            source = ROOT / name
            if source.is_file():
                (work / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, work / name)
        configure = run(["cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"], work)
        if configure.returncode != 0:
            sys.exit(configure.stdout + configure.stderr)
        original = (work / CONTENT).read_text()

        full = check(work, original, row(WEAPON_COLUMNS), row(PERK_COLUMNS), with_tests=True)
        print(f"whole rows (2 edit sites in {CONTENT}) | {full[0]} | {full[1]}", flush=True)
        silent = 0
        for kind, columns, other in (("weapon", WEAPON_COLUMNS, PERK_COLUMNS), ("perk", PERK_COLUMNS, WEAPON_COLUMNS)):
            for name, _ in columns:
                partial = row(columns, drop=name)
                rows = (partial, row(other)) if kind == "weapon" else (row(other), partial)
                result, message = check(work, original, *rows, with_tests=True)
                if result != "build fails":
                    silent += 1
                print(f"{kind} row without {name} | {result} | {message}", flush=True)
        print(f"silent omissions: {silent}")
        if full[0] != "builds, tests pass" or silent:
            sys.exit(1)
    finally:
        if keep:
            print(f"kept {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
