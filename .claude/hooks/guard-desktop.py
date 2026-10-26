#!/usr/bin/env python3
"""PreToolUse hook: block Bash commands that can break the user's KDE session or play audio.

A nested KWin on the user's session bus took over their global shortcuts and left
them all inactive when it exited. XTEST input into the user's own Xwayland (:0)
desynced KWin's input path. A game run without --mute played on the user's speakers.
Use the verify skill (`zv launch`), which mutes the game and runs a private headless
gamescope on its own bus.

The launch check reads shell words, not a full shell grammar. It finds the game when it
is the command of a simple command, or any word after a launcher such as gamescope, gdb
or timeout, also inside `bash -c`. It ignores heredoc bodies, comments and quoted text.
Run with --selftest to check the rules.
"""

import json
import re
import shlex
import sys

ZV = "Use `zv launch` (.claude/skills/verify/bin/zv), which mutes the game and runs a private headless gamescope"

RULES: list[tuple[str, str]] = [
    (r"\bkwin_(wayland|x11)\b", "a nested KWin shares the user's session bus and breaks their global shortcuts"),
    (r"\bkwriteconfig\d*\b.*\b(kwinrc|kglobalshortcutsrc|kxkbrc)\b", "this edits the user's live KDE config"),
    (r"\bDISPLAY=:0\b", "this targets the user's own desktop display"),
]

# The program to run follows these words, after their options. Any game word after one counts as a launch.
LAUNCHERS = {"env", "exec", "nohup", "setsid", "nice", "ionice", "stdbuf", "timeout", "time", "xvfb-run",
             "dbus-run-session", "gamescope", "strace", "ltrace", "valgrind", "gdb", "perf", "taskset", "mangohud",
             "prime-run", "script", "bash", "sh", "source", "."}
KEYWORDS = {"then", "do", "else", "elif", "if", "while", "until", "!", "{"}
ASSIGNMENT = re.compile(r"\w+=.*")
GAME = re.compile(r"\S*/zombies|(\S*/)?run\.sh")
MUTE_VAR = re.compile(r"ZOMBIES_MUTE=(?!0?$).*")
SEPARATOR = re.compile(r"[;&|()\n]+")


def lex(text: str) -> list[str]:
    lexer = shlex.shlex(text, posix=True, punctuation_chars=";&|()<>")
    lexer.whitespace_split = True
    return list(lexer)


def words_of(command: str) -> list[str]:
    """Shell words, one line at a time. A line with an open quote joins the next. Heredoc bodies are dropped."""
    words: list[str] = []
    delimiters: list[str] = []
    pending = ""
    for line in command.split("\n"):
        if delimiters:
            if line.strip() == delimiters[0]:
                delimiters.pop(0)
            continue
        pending = f"{pending}\n{line}" if pending else line
        try:
            line_words = lex(pending)
        except ValueError:
            continue
        pending = ""
        delimiters += [after.lstrip("-") for word, after in zip(line_words, line_words[1:]) if word == "<<"]
        words += [*line_words, "\n"]
    return words + pending.split()


def simple_commands(command: str) -> list[list[str]]:
    segments: list[list[str]] = [[]]
    for word in words_of(command):
        if SEPARATOR.fullmatch(word):
            segments.append([])
        else:
            segments[-1].append(word)
    return [segment for segment in segments if segment]


def unmuted_launch(command: str, muted: bool = False) -> bool:
    """True when a simple command starts the game, directly or after a launcher word, with no mute for it."""
    segments = simple_commands(command)
    muted = muted or any(MUTE_VAR.fullmatch(word) for segment in segments if segment[0] == "export" for word in segment)
    for segment in segments:
        i = 0
        while i < len(segment) and (ASSIGNMENT.fullmatch(segment[i]) or segment[i] in KEYWORDS):
            i += 1
        segment_muted = muted or "--mute" in segment or any(MUTE_VAR.fullmatch(word) for word in segment)
        rest = [word.strip("`") for word in segment[i:]]
        if not rest:
            continue
        program = rest[0].rsplit("/", 1)[-1] if rest[0] != "." else "."
        if program in LAUNCHERS:
            if program in ("bash", "sh"):
                for flag, script in zip(rest, rest[1:]):
                    if flag.startswith("-") and flag.endswith("c") and unmuted_launch(script, segment_muted):
                        return True
            launches = any(GAME.fullmatch(word) for word in rest[1:])
        else:
            launches = GAME.fullmatch(rest[0]) is not None
        if launches and not segment_muted:
            return True
    return False


def block_reason(command: str) -> str | None:
    for pattern, reason in RULES:
        if re.search(pattern, command):
            return f"{reason}. Use .claude/skills/verify/bin/zv (private headless gamescope) instead."
    if unmuted_launch(command):
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
    ('"./build/zombies" --smoke', True),
    ("'./run.sh'", True),
    ('"$PWD"/build/zombies --smoke', True),
    ("(./build/zombies --smoke)", True),
    ("env -i ./build/zombies --smoke", True),
    ("bash -c './build/zombies --smoke'", True),
    ("bash -lc ./run.sh", True),
    ("nice ./build/zombies --smoke", True),
    ("timeout -s KILL 5 ./build/zombies", True),
    ("timeout --signal=INT 5 ./build/zombies", True),
    ("strace -f ./build/zombies", True),
    ("gdb --args ./build/zombies", True),
    ("sh -c ./build/zombies", True),
    ("./build/zombies>log", True),
    ("gamescope -- ./build/zombies", True),
    ("gamescope --backend headless -W 1920 -H 1080 -r 60 -- ./build/zombies --smoke", True),
    ("dbus-run-session -- gamescope --backend headless -- ./build/zombies", True),
    ("gdb -batch -ex run -ex bt --args ./build/zombies --smoke", True),
    ("strace -f -o /tmp/t ./build/zombies", True),
    ("perf record -g ./build/zombies --smoke", True),
    ("cat > n.md <<-EOF\n\tnotes\n\tEOF\n./build/zombies --smoke", True),
    ("cat > n.md <<'EOF'\nSee issue #15\nEOF\n./build/zombies --smoke", True),
    ("cat > n.md <<'EOF'\nDon't run it\nEOF\n./build/zombies --smoke", True),
    ('git commit -m "#15"; ./build/zombies', True),
    ("grep -c '#' src/main.c && ./build/zombies --smoke", True),
    ("# don't forget to rebuild\ncmake --build build && ./build/zombies --smoke", True),
    ("cmake --build build -j4  # it's quick\n./build/zombies --smoke", True),
    ('git commit -m "docs: explain <<EOF handling"\n./build/zombies --smoke', True),
    ("cat <<END-OF\nx\nEND-OF\n./build/zombies", True),
    ("cat <<-EOF\n\tx\n\tEOF\n./build/zombies", True),
    (". ./run.sh --smoke", True),
    ("source run.sh", True),
    ("/usr/bin/gamescope -- ./build/zombies", True),
    ("{ ./build/zombies; }", True),
    ("if true; then ./build/zombies; fi", True),
    ("while :; do ./build/zombies; done", True),
    ("./build/zombies --smoke; echo --mute", True),
    ("./build/zombies --windowed # --mute", True),
    ("ZOMBIES_MUTE=1 true; ./build/zombies", True),
    ("ZOMBIES_MUTE=0 ./build/zombies", True),
    ("cat <<'X'\nnotes\nX\n./build/zombies", True),
    ("kwin_wayland --virtual", True),
    ("DISPLAY=:0 xdotool key a", True),
    ("zv launch --run smoke-1 -- --smoke", False),
    (".claude/skills/verify/bin/zv launch --run a --output 1920x1080", False),
    ("./build/zombies --mute --smoke", False),
    ("ZOMBIES_MUTE=1 ./build/zombies --smoke", False),
    ("ZOMBIES_MUTE=yes ./build/zombies --smoke", False),
    ("export ZOMBIES_MUTE=1; ./build/zombies --smoke", False),
    ("bash -c './build/zombies --mute'", False),
    ("ZOMBIES_MUTE=1 bash -c './build/zombies --smoke'", False),
    ("if grep -q mute run.sh; then echo ok; fi", False),
    ("if [ -x build/zombies ]; then echo built; fi", False),
    ("if true; then ls build/zombies; fi", False),
    ("{ cat run.sh; } > /tmp/x", False),
    ("! grep -q mute run.sh", False),
    ("watch -n1 ls -l build/zombies", False),
    ("sudo cp build/zombies /usr/local/bin/", False),
    ("command -v ./build/zombies", False),
    ('git commit -m "first line\n\n./build/zombies now needs --mute"', False),
    ("git commit -m \"$(cat <<'EOF'\nDon't run ./build/zombies; use zv\nEOF\n)\"", False),
    ("gh pr create --body-file - <<'EOF'\n## Summary\n./run.sh is blocked, see #15\nEOF", False),
    ("ctest --test-dir build --output-on-failure", False),
    ("./build/zombies_test", False),
    ("cmake --build build -j", False),
    ("cat run.sh", False),
    ("ls -l build/zombies", False),
    ("git diff -- run.sh src/main.c", False),
    ("rg '(./run.sh)' .", False),
    ("git commit -m 'guard: block; ./run.sh without mute'", False),
    ("cat > notes.md <<'X'\n./build/zombies --smoke\n./run.sh\nX", False),
    ("gh pr create --body-file - <<EOF\nNever run ./build/zombies directly.\nEOF\necho done", False),
]


def selftest() -> int:
    failures = 0
    for command, blocked in SELFTEST:
        reason = block_reason(command)
        ok = (reason is not None) == blocked and (reason is None or "zv" in reason)
        failures += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {'block' if reason else 'allow'}  {command!r}")
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
