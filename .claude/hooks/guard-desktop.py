#!/usr/bin/env python3
"""PreToolUse hook: block Bash commands that can break the user's KDE session or play audio.

A nested KWin on the user's session bus took over their global shortcuts and left
them all inactive when it exited. XTEST input into the user's own Xwayland (:0)
desynced KWin's input path. A game run without --mute played on the user's speakers.
Use the verify skill (`zv launch`), which mutes the game and runs a private headless
gamescope on its own bus.

The launch check scans shell text in one pass, not with a full shell grammar. It finds the
game when it is the command of a simple command, or any word after a launcher such as
gamescope, gdb or timeout. It also checks `bash -c` scripts, `$(...)`, and heredocs fed to a
shell. Other heredoc bodies, comments and quoted text are data.
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

# The program to run follows these words, after their options. Any game word after one counts as a launch.
SHELLS = {"bash", "sh", "zsh", "fish", "dash"}
LAUNCHERS = SHELLS | {
    "env", "exec", "nohup", "setsid", "nice", "ionice", "chrt", "taskset", "stdbuf", "unbuffer", "timeout", "time",
    "xvfb-run", "dbus-run-session", "gamescope", "cage", "weston", "xterm", "konsole", "systemd-run", "firejail",
    "gamemoderun", "mangohud", "prime-run", "script", "source", ".", "gdb", "lldb", "rr", "strace", "ltrace",
    "valgrind", "heaptrack", "perf", "catchsegv", "apitrace", "renderdoccmd", "hyperfine",
}
KEYWORDS = {"then", "do", "else", "elif", "if", "while", "until", "!", "{"}
ASSIGNMENT = re.compile(r"\w+=.*", re.DOTALL)
GAME = re.compile(r"(.*/)?run\.sh|.*/zombies", re.DOTALL)
MUTE_VAR = re.compile(r"ZOMBIES_MUTE=(?!0?$).*", re.DOTALL)
DELIMITER = re.compile(r"[ \t]*(\\?)(['\"]?)([^\s'\";&|()<>\\]+)\2")

# Code found inside a command: its text, the index of the simple command it belongs to, and whether it always runs.
# A heredoc body runs only when that simple command feeds a shell. The `$(...)` and backticks in a body with an
# unquoted delimiter always run.
Nested = tuple[str, int, bool]


def body_substitutions(body: str) -> list[str]:
    """The code in `$(...)` and backticks of a heredoc body with an unquoted delimiter. Quotes are text there."""
    found: list[str] = []
    i = 0
    n = len(body)
    while i < n:
        if body[i] == "\\":
            i += 2
        elif body.startswith("$((", i):
            end = body.find("))", i + 3)
            i = n if end < 0 else end + 2
        elif body.startswith("$(", i):
            _, _, end = scan(body, i + 2, closer=True)
            found.append(body[i + 2:end])
            i = end + 1
        elif body[i] == "`":
            end = i + 1
            while end < n and body[end] != "`":
                end += 2 if body[end] == "\\" else 1
            found.append(body[i + 1:end])
            i = end + 1
        else:
            i += 1
    return found


def scan(t: str, i: int = 0, closer: bool = False) -> tuple[list[list[str]], list[Nested], int]:
    """Split shell text into simple commands of unquoted words, one pass.

    Handles quotes, backslash line joins, comments, heredocs, `$(...)`, backticks and process
    substitution. With closer set, stops at the `)` that ends a `$(` and returns its index.
    """
    segments: list[list[str]] = [[]]
    nested: list[Nested] = []
    heredocs: list[tuple[str, bool, bool, int]] = []
    word: list[str] | None = None
    depth = 0
    n = len(t)

    def add(text: str) -> None:
        nonlocal word
        if word is None:
            word = []
        word.append(text)

    def end_word() -> None:
        nonlocal word
        if word is not None:
            segments[-1].append("".join(word))
            word = None

    def new_segment() -> None:
        end_word()
        if segments[-1]:
            segments.append([])

    def substitution(start: int) -> int:
        _, _, end = scan(t, start, closer=True)
        nested.append((t[start:end], len(segments) - 1, True))
        return end

    def arithmetic_end(start: int) -> int:
        end = t.find("))", start)
        return n if end < 0 else end + 2

    def read_double(i: int) -> tuple[str, int]:
        out: list[str] = []
        while i < n:
            c = t[i]
            if c == '"':
                return "".join(out), i + 1
            if c == "\\":
                out.append(t[i + 1:i + 2])
                i += 2
            elif t.startswith("$((", i):
                end = arithmetic_end(i + 3)
                out.append(t[i:end])
                i = end
            elif t.startswith("$(", i):
                end = substitution(i + 2)
                out.append(t[i:end + 1])
                i = end + 1
            elif c == "`":
                end = t.find("`", i + 1)
                end = n if end < 0 else end
                nested.append((t[i + 1:end], len(segments) - 1, True))
                i = end + 1
            else:
                out.append(c)
                i += 1
        return "".join(out), n

    while i < n:
        c = t[i]
        if c == "\\":
            if t[i + 1:i + 2] != "\n":
                add(t[i + 1:i + 2])
            i += 2
        elif c == "'":
            end = t.find("'", i + 1)
            end = n if end < 0 else end
            add(t[i + 1:end])
            i = end + 1
        elif t.startswith("$'", i):
            end = i + 2
            while end < n and t[end] != "'":
                end += 2 if t[end] == "\\" else 1
            add(t[i + 2:end])
            i = end + 1
        elif c == '"':
            text, i = read_double(i + 1)
            add(text)
        elif c == "`":
            end = t.find("`", i + 1)
            end = n if end < 0 else end
            nested.append((t[i + 1:end], len(segments) - 1, True))
            add(t[i:end + 1])
            i = end + 1
        elif t.startswith("$((", i):
            end = arithmetic_end(i + 3)
            add(t[i:end])
            i = end
        elif t.startswith(("$(", "<(", ">("), i):
            end = substitution(i + 2)
            add(t[i:end + 1])
            i = end + 1
        elif c == "#" and word is None:
            end = t.find("\n", i)
            i = n if end < 0 else end
        elif c in " \t\r":
            end_word()
            i += 1
        elif c == "\n":
            new_segment()
            i += 1
            for delimiter, strip, quoted, index in heredocs:
                start = body_end = i
                while i < n:
                    end = t.find("\n", i)
                    end = n if end < 0 else end
                    line = t[i:end]
                    i = min(n, end + 1)
                    if (line.lstrip("\t") if strip else line) == delimiter:
                        break
                    body_end = i
                body = t[start:body_end]
                nested.append((body, index, False))
                if not quoted:
                    nested += [(code, index, True) for code in body_substitutions(body)]
            heredocs = []
        elif c == ")" and closer and depth == 0:
            end_word()
            return segments, nested, i
        elif c in "()":
            new_segment()
            depth = max(0, depth + (1 if c == "(" else -1))
            i += 1
        elif t.startswith(("&>", ">&", "<&"), i):
            end_word()
            i += 2
        elif c in ";&|":
            new_segment()
            i += 1
        elif t.startswith("<<<", i):
            end_word()
            i += 3
        elif t.startswith("<<", i):
            end_word()
            strip = t.startswith("<<-", i)
            match = DELIMITER.match(t, i + 2 + strip)
            if match:
                heredocs.append((match[3], strip, bool(match[1] or match[2]), len(segments) - 1))
                i = match.end()
            else:
                i += 2 + strip
        elif c in "<>":
            end_word()
            i += 1
        else:
            add(c)
            i += 1
    end_word()
    return segments, nested, n


def command_words(segment: list[str]) -> list[str]:
    i = 0
    while i < len(segment) and (ASSIGNMENT.fullmatch(segment[i]) or segment[i] in KEYWORDS):
        i += 1
    return segment[i:]


def program(words: list[str]) -> str:
    return words[0].rsplit("/", 1)[-1] if words and words[0] != "." else "".join(words[:1])


def feeds_shell(words: list[str]) -> bool:
    name = program(words)
    return name in SHELLS or (name in LAUNCHERS and any(word.rsplit("/", 1)[-1] in SHELLS for word in words[1:]))


def unmuted_launch(command: str, muted: bool = False) -> bool:
    """True when a simple command starts the game, directly or after a launcher word, with no mute for it."""
    segments, nested, _ = scan(command)
    muted = muted or any(MUTE_VAR.fullmatch(word) for segment in segments if segment[:1] == ["export"] for word in segment)
    segment_muted = [muted or "--mute" in segment or any(MUTE_VAR.fullmatch(word) for word in segment)
                     for segment in segments]
    for text, index, always_runs in nested:
        if (always_runs or feeds_shell(command_words(segments[index]))) and unmuted_launch(text, segment_muted[index]):
            return True
    for segment, segment_is_muted in zip(segments, segment_muted):
        words = command_words(segment)
        if not words:
            continue
        if program(words) in LAUNCHERS:
            if not segment_is_muted and any(GAME.fullmatch(word) for word in words[1:]):
                return True
            if any(" " in word and unmuted_launch(word, segment_is_muted) for word in words[1:]):
                return True
        elif GAME.fullmatch(words[0]) and not segment_is_muted:
            return True
    return False


def block_reason(command: str) -> str | None:
    for pattern, reason in RULES:
        if re.search(pattern, command):
            return f"{reason}. Use .claude/skills/verify/bin/zv (private headless gamescope) instead."
    try:
        launch = unmuted_launch(command)
    except RecursionError:
        return "this command nests too deep to check for a game launch. Split it into smaller commands."
    if launch:
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
    ("timeout 5 bash -c './build/zombies --smoke'", True),
    ("timeout 30 sh -c 'cd build && ./zombies --windowed'", True),
    ("dbus-run-session -- bash -c 'gamescope --backend headless -- ./build/zombies'", True),
    ("nohup sh -c './run.sh --smoke' > /tmp/log 2>&1 &", True),
    ("gamescope --backend headless -W 1920 -H 1080 \\\n  -- ./build/zombies --smoke", True),
    ("cmake --build build -j4 && \\\n./build/zombies --smoke", True),
    ("bash <<'EOF'\ncd build\n./zombies --smoke\nEOF", True),
    ("bash -s <<EOF\n./build/zombies --smoke\nEOF", True),
    ("rg -n '<<' .claude/hooks/guard-desktop.py\n./build/zombies --smoke", True),
    ("lldb -- ./build/zombies --smoke", True),
    ("fish -c './build/zombies --smoke'", True),
    ("hyperfine './build/zombies --smoke'", True),
    ('"$(git rev-parse --show-toplevel)/build/zombies" --smoke', True),
    ("sed -i s#foo#bar#g src/x.c && ./build/zombies --smoke", True),
    ("git commit -m \"$(cat <<'EOF'\nfix: support 27\" monitors\nEOF\n)\"\n./build/zombies --smoke", True),
    ("echo $(./build/zombies --smoke)", True),
    ("timeout 30 bash <<'EOF'\n./build/zombies --smoke\nEOF", True),
    ("env ZOMBIES_X=1 bash <<'EOF'\n./build/zombies --smoke\nEOF", True),
    ("gh pr create --title t --body \"$(cat <<EOF\n## Test plan\n- `./build/zombies --smoke`\nEOF\n)\"", True),
    ("cat > notes.md <<EOF\nRan `./build/zombies --smoke`\nEOF", True),
    ("cat > notes.md <<EOF\nRan $(./build/zombies --smoke)\nEOF", True),
    ("cat <<\\EOF\nx\nEOF\n./build/zombies", True),
    ("cat > notes.md <<EOF\nPath \"C:\\\\\" isn't `./build/zombies --smoke`\nEOF", True),
    ("cat > notes.md <<EOF\nPath \"C:\\\\\" # `./build/zombies --smoke`\nEOF", True),
    ("cat > notes.md <<EOF\nRegex \"\\\\\\\\\" won't match; $(./build/zombies --smoke)\nEOF", True),
    ("./build/zombies --smoke 2>&1 | tee log", True),
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
    ("ZOMBIES_MUTE=1 \\\n  ./build/zombies --smoke", False),
    ("./build/zombies --smoke \\\n  --mute", False),
    ("./build/zombies 2>&1 --mute | tee log", False),
    ("git commit -m \"$(cat <<'EOF'\nfix: support 27\" monitors\n\n./build/zombies --smoke now mutes\nEOF\n)\"", False),
    ("gh pr comment 26 --body \"$(cat <<'EOF'\nRun ./build/zombies --mute, not ./run.sh.\nEOF\n)\"", False),
    ("echo $((1<<4))", False),
    ("cat <<< './build/zombies'", False),
    ("cat > notes.md <<'EOF'\nRan `./build/zombies --smoke`\nEOF", False),
    ("cat > notes.md <<EOF\nRan ./build/zombies --smoke and \"$HOME\" \\ ok\nEOF", False),
    ("cat <<\\EOF\n`./build/zombies`\nEOF", False),
    ("gh pr create --title t --body \"$(cat <<EOF\n## Test plan\n- \\`./build/zombies --smoke\\` is blocked\nEOF\n)\"", False),
    ("cat > notes.md <<EOF\nDo not run \\`./build/zombies --smoke\\` directly.\nEOF", False),
    ("cat > notes.md <<EOF\nEscaped: \\$(./build/zombies --smoke) is text\nEOF", False),
    ("cat > notes.md <<EOF\nA \\\" quote, a \\\\ and `./build/zombies --mute`\nEOF", False),
    ("cat > notes.md <<EOF\nWindows path \"C:\\\\\" is fine\n./build/zombies --smoke\nEOF", False),
    ("cat > notes.md <<EOF\nprintf(\"\\\\\"); then\n./build/zombies --smoke\nEOF", False),
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
