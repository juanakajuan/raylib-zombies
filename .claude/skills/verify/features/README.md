# raylib-zombies verification map

This directory is the maintained source for verifying the user-facing behavior of raylib-zombies. Read this index before driving the game, then use the matching feature file as the recipe.

## Baseline preconditions

- `zv` means `.claude/skills/verify/bin/zv`, run from the repo root. Start each shell command with `zv() { .claude/skills/verify/bin/zv "$@"; }`.
- Launch a fresh game for every recipe: `zv launch --run <feature>-<n>`. A fresh game is round 1, 500 points, M1911 `8 / 96`, 100 health, front room only, 5 s "Get ready" countdown.
- Run `zv doctor --run <name>` after launch and require `OK`.
- Never drive a game that this verification run did not launch. The user may have their own copy open on the desktop.
- Zombie spawns depend on the clock. Results such as points after a round differ between runs. Assert ranges and relations (points went down by exactly the cost), not exact values.

## Driving conventions

- Pass `--run <name>` to every command when more than one run exists.
- Start every recipe from the start position and view. The walking routes in [SKILL.md](../SKILL.md#drive) assume them.
- Chain a hunt and the purchase that follows it in one shell command. Zombies attack while the agent thinks between tool calls.
- Buy during the intermission after `zv hunt --until-clear`, when no zombie is alive.
- Treat every command as literal. Key names are X keysyms and are case sensitive (`Escape`, `F11`, `Shift_L`).
- Stop the run at the end, and after every failed attempt: `zv stop --run <name>`.

## Proof and skip reporting

- Number screenshots in action order: `zv shot 01-before`, `zv shot 02-after`.
- Capture the state before the action and the state after it. `actions.log` in the run directory records the input between them.
- For a purchase, prove both the points drop (`--crop points` before and after) and the effect in the world.
- Report the run directory `verify-artifacts/<run>/` and the shot names.
- If the bot dies before it earns enough points, report the journey as failed with that reason. Do not report it as verified through `--smoke` or a code change.

## Feature entry contract

Each feature file starts with an H1 title and one paragraph describing the user-visible behavior. It then uses exactly four H2 sections in this order.

1. `Sub-features` lists short IDs with one line for each behavior.
2. `How to get to it (user POV)` lists every user entry point.
3. `Driving it with zv` starts with `Preconditions:` and uses labeled bullets that pair each user action with an exact command and observable result.
4. `Gotchas` lists traps that can waste or invalidate a verification run.

## Features

- [Combat and rounds](./combat-and-rounds.md) covers firing, hits and kills for points, reload, the round counter and the intermission.
- [Open the door](./door.md) covers buying the 750 point door and entering the back room.
- [Wall-buy weapons](./wall-buys.md) covers buying the MP5K and shotgun, carrying two weapons, swapping with Q and refilling ammo.
- [Vitality Brew perk](./perk.md) covers buying the 2500 point perk that raises max health to 250.
- [Pause and display mode](./pause-and-display.md) covers Esc pause, quit from pause, F11 and Alt+Enter, and the `--fullscreen` and `--windowed` flags.
- [Game over and restart](./game-over-and-restart.md) covers death, the game over screen and restart with R.
- [Debug tools](./debug-tools.md) covers the `--debug` flag and the F1 cheat menu for points, weapons, perks, god mode and round skipping.
- [Smoke mode](./smoke-mode.md) covers the `--smoke` scripted run that writes three screenshots.
