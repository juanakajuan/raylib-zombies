#!/usr/bin/env python3
"""PreToolUse hook: block Bash commands that can break the user's KDE session.

A nested KWin on the user's session bus took over their global shortcuts and left
them all inactive when it exited. XTEST input into the user's own Xwayland (:0)
desynced KWin's input path. Use the verify skill (`zv`), which runs a private
headless gamescope on its own bus.
"""

import json
import re
import sys

RULES: list[tuple[str, str]] = [
    (r"\bkwin_(wayland|x11)\b", "a nested KWin shares the user's session bus and breaks their global shortcuts"),
    (r"\bkwriteconfig\d*\b.*\b(kwinrc|kglobalshortcutsrc|kxkbrc)\b", "this edits the user's live KDE config"),
    (r"\bDISPLAY=:0\b", "this targets the user's own desktop display"),
]

payload: object = json.load(sys.stdin)
command = ""
if isinstance(payload, dict):
    tool_input = payload.get("tool_input")
    if isinstance(tool_input, dict) and isinstance(tool_input.get("command"), str):
        command = tool_input["command"]

for pattern, reason in RULES:
    if re.search(pattern, command):
        print(f"Blocked: {reason}. Use .claude/skills/verify/bin/zv (private headless gamescope) instead.",
              file=sys.stderr)
        sys.exit(2)
