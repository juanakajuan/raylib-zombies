#!/usr/bin/env python3
"""PreToolUse hook: block Bash commands that can break the user's KDE session or play audio.

A nested KWin on the user's session bus took over their global shortcuts and left
them all inactive when it exited. XTEST input into the user's own Xwayland (:0)
desynced KWin's input path. A game run without --mute played on the user's speakers.
Use the verify skill (`zv launch`), which mutes the game and runs a private headless
gamescope on its own bus.

Run with --selftest to check the rules.
"""

import json
import re
import sys

ZV = "Use `zv launch` (.claude/skills/verify/bin/zv), which mutes the game and runs a private headless gamescope"

RULES: list[tuple[str, str]] = [
    (r"\bkwin_(wayland|x11)\b", "a nested KWin shares the user's session bus and breaks their global shortcuts"),
    (r"\bkwriteconfig\d*\b.*\b(kwinrc|kglobalshortcutsrc|kxkbrc)\b", "this edits the user's live KDE config"),
    (r"\bDISPLAY=:0\b", "this targets the user's own desktop display"),
]

# The game binary or run.sh at the start of a shell command, after any VAR=value and wrapper words.
GAME_LAUNCH = re.compile(
    r"(?:^|[;&|(`\n]|\$\()\s*"
    r"(?:\w+=\S*\s+|(?:env|exec|nohup|setsid|command|sh|bash|timeout\s+\S+)\s+)*"
    r"(?:\S*/zombies|(?:\S*/)?run\.sh)(?=\s|$|[;&|)`])"
)
MUTED = re.compile(r"(?:^|\s)--mute(?=\s|$|[;&|)`])|\bZOMBIES_MUTE=1\b")


def block_reason(command: str) -> str | None:
    for pattern, reason in RULES:
        if re.search(pattern, command):
            return f"{reason}. Use .claude/skills/verify/bin/zv (private headless gamescope) instead."
    if GAME_LAUNCH.search(command) and not MUTED.search(command):
        return f"this runs build/zombies without --mute or ZOMBIES_MUTE=1, so it can play audio on the user's speakers. {ZV}."
    return None


SELFTEST: list[tuple[str, bool]] = [
    ("./build/zombies --smoke", True),
    ("./run.sh --smoke", True),
    ("bash run.sh", True),
    ("timeout 5 ./build/zombies --windowed", True),
    ("cd build && ./zombies --windowed", True),
    ("cmake --build build && ./build/zombies", True),
    ("FOO=1 /home/u/raylib-zombies/build/zombies", True),
    ("kwin_wayland --virtual", True),
    ("DISPLAY=:0 xdotool key a", True),
    ("zv launch --run smoke-1 -- --smoke", False),
    (".claude/skills/verify/bin/zv launch --run a --output 1920x1080", False),
    ("./build/zombies --mute --smoke", False),
    ("ZOMBIES_MUTE=1 ./build/zombies --smoke", False),
    ("ctest --test-dir build --output-on-failure", False),
    ("./build/zombies_test", False),
    ("cmake --build build -j", False),
    ("cat run.sh", False),
    ("ls -l build/zombies", False),
    ("git diff -- run.sh src/main.c", False),
]


def selftest() -> int:
    failures = 0
    for command, blocked in SELFTEST:
        reason = block_reason(command)
        ok = (reason is not None) == blocked and (reason is None or "zv" in reason)
        failures += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {'block' if reason else 'allow'}  {command}")
        if reason:
            print(f"       {reason}")
    print(f"{len(SELFTEST) - failures} of {len(SELFTEST)} cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    payload: object = json.load(sys.stdin)
    command = ""
    if isinstance(payload, dict):
        tool_input = payload.get("tool_input")
        if isinstance(tool_input, dict) and isinstance(tool_input.get("command"), str):
            command = tool_input["command"]
    reason = block_reason(command)
    if reason is not None:
        print(f"Blocked: {reason}", file=sys.stderr)
        sys.exit(2)
