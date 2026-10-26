---
name: verify
description: Launch and drive the raylib-zombies first-person game (C11, raylib 5.5, X11 window) in a private headless gamescope display with real keyboard and mouse input, and capture screenshot evidence. Use to prove a gameplay, HUD, purchase, pause, or display-mode change works in the real game, not only in tests/game_test.c.
---

# Verify raylib-zombies

The game is a desktop window. The user touches it only through keyboard, mouse and the screen. This skill runs the real binary inside `gamescope --backend headless`, sends real X input through XTEST, and reads the real window pixels. It never touches the user's own desktop.

All commands use the helper `.claude/skills/verify/bin/zv` from the repo root. Shell state does not persist between tool calls, so start each shell command with this function and then write `zv`:

```sh
zv() { .claude/skills/verify/bin/zv "$@"; }
```

`zv --help` and `zv <command> --help` list every flag.

Secondary surfaces:

- `./build/zombies --smoke` is a scripted bot that cheats (3000 points, preset seed). It is a CI-style check, not a user path. See [features/smoke-mode.md](features/smoke-mode.md).
- `ctest --test-dir build --output-on-failure` runs the headless simulation test. It does not prove render or input.

## Requirements

- `gamescope` (headless backend), `cmake`, a C compiler, `python3` with `Xlib` (python-xlib) and `PIL` (Pillow). On this machine all are present.
- The first configure downloads raylib 5.5 with CMake `FetchContent`. In a git worktree of this repo, reuse the main checkout's copy instead: `export FETCHCONTENT_SOURCE_DIR_RAYLIB=<main checkout>/build/_deps/raylib-src` before `zv launch` or `zv build`.

## Launch

```sh
zv launch --run <name>                     # builds, then starts the game
zv launch --run <name> -- --fullscreen     # game arguments after --
```

- `--output WxH` sets the headless monitor size (default `1280x720`, the game's window size). Use `--output 1920x1080` to test fullscreen, because then fullscreen is larger than the window.
- `launch` runs `cmake` configure (only when `build/CMakeCache.txt` is missing) and `cmake --build build -j`, then starts `gamescope --backend headless -W 1280 -H 720 -- build/zombies`. Use `--no-build` to skip the build when you just built.
- The game runs with `LIBGL_ALWAYS_SOFTWARE=1` (llvmpipe). With hardware GL the window pixels that X returns are stale. Do not remove this.
- Ready means: the game log has `TIMER: Target time per frame` and the window has drawn a HUD frame. `launch` waits for this and prints `ready run=<name> display=:N dir=<path>`. On failure it exits non-zero and names the log to read.
- There is no data directory and no seed data. Each launch is a fresh game: round 1, 500 points, M1911 `8 / 96`, 100 health, 5 s "Get ready" countdown. The RNG seed is the clock, so zombie spawns differ every run.
- The game's working directory is the run directory, so a raylib `F12` screenshot (`screenshot000.png`) lands in the evidence.

Isolation: every launch gets its own gamescope, its own X display number (gamescope picks a free one) and its own run directory. Two runs can go side by side; pass `--run <name>` to every command so each drives its own game. Without `--run`, commands use `verify-artifacts/latest`, which is the most recent launch.

## Doctor

```sh
zv doctor --run <name>
```

Read-only. It reports the display, PIDs, git commit and game arguments, and fails (`PROBLEM ...`, exit 1) when gamescope or the game is dead, the game PID runs a different binary, `build/zombies` was rebuilt after launch, or `src/` changed after the binary was built. It then checks the window is 1280x720 and whether frames advance over 1.2 s (they do not change while paused or on a static screen, which is normal). Run it first whenever an action seems to do nothing.

## Drive

Each command sends real input to the game window. They return when the input is sent; the game reacts within one frame.

| Command | User action |
| --- | --- |
| `zv key w --hold 1.0` | Hold a key for a time. Names are X keysyms: `w a s d e r q Escape F11 F12 Return Shift_L Alt_L`. |
| `zv key w+Shift_L --hold 1.0` | Hold several keys together (sprint forward). `Alt_L+Return` toggles fullscreen. |
| `zv key r --repeat 3 --gap 0.2` | Tap a key several times. |
| `zv click --count 3` | Fire. Default 0.35 s between clicks (M1911 fires every 0.28 s, semi-automatic). |
| `zv click --hold 2.0` | Hold fire (automatic MP5K). |
| `zv wheel down` / `zv wheel up` | Scroll one notch. Down selects the next weapon, up the previous. |
| `zv look 90` / `zv look -45 10` | Turn the view by degrees: yaw positive right, pitch positive up. Exact: one mouse pixel is 0.0022 rad. |
| `zv window` | Print the window size and `mode windowed` or `mode fullscreen`. Read-only. |
| `zv zombies` | List zombie heads visible on screen (pixel position and size). Read-only. |
| `zv hunt --seconds 60 --until-clear --stay --restore-view` | Bot: find a zombie, turn to it, click. Reloads after 8 clicks. `--until-clear` stops when the red `ROUND N` or `GAME OVER` banner appears. `--stay` never steps back, so the player keeps its position. `--restore-view` turns back to the view direction it started with. |
| `zv shot <name> [--crop points]` | Save the window to `<run>/shots/<name>.png`. `--crop` also saves a 2x zoom of one HUD region: `points round health weapon prompt banner center` (1280x720 window only). |

Map and movement facts (from `src/game.c`): the player starts at the centre of the front room facing the door, walk speed 4.5 m/s, sprint 6.5 m/s. Walls stop the player, so over-long holds are safe. Measured routes from the start position, before any turn:

| Destination | Keys | Expected prompt |
| --- | --- | --- |
| MP5K wall buy (left wall, orange panel) | `zv key a --hold 2.0` then `zv key w --hold 0.65` | `Press E to buy MP5K [1000]` |
| Shotgun wall buy (right wall, red panel) | `zv key d --hold 2.0` then `zv key w --hold 0.45` | `Press E to buy Shotgun [1500]` |
| Door to the back room | `zv key w --hold 2.5` | `Press E to open the door [750]` |
| Vitality Brew perk (blue machine, back right corner) | `zv key d --hold 2.0` then `zv key s --hold 0.6` | `Press E to drink Vitality Brew [2500]` |

The prompt text is red when the player cannot afford it and white when it can. Points come only from shooting zombies: 10 per hit, 60 per kill, 100 per headshot kill. A full round 1 with `hunt` typically gives 1100 to 1300 points. To buy, run `hunt --until-clear --restore-view` in the open from the start position, then walk to the spot and press `e` (hunting with `--stay` while standing at a purchase spot got the bot killed in round 1 in most attempts). Chain these in one shell command: a pause between tool calls lets zombies kill the player.

## Evidence

Every run writes to `verify-artifacts/<run>/` at the repo root (git-ignored):

- `state.json`: display, PIDs, git commit, dirty flag, binary mtime, game arguments, start and stop times.
- `actions.log`: a timestamped line for every input and screenshot command. This is the record of the user path.
- `shots/*.png`: window screenshots, plus `*.<region>.png` crops.
- `build.log` (cmake output of `launch`), `game.log` (raylib log), `gamescope.log`.

Read screenshots with the Read tool. There is no OCR on this machine, so read HUD numbers from the image or its `--crop` zoom.

Proof standards:

- Drive the real user path: keys, mouse, and points earned by shooting. Do not add cheats, change `START_POINTS`, or use `--smoke` as proof of a user feature.
- Capture before and after: a shot of the state before the action, the action in `actions.log`, and a shot of the result. Number shots in order (`01-...`, `02-...`).
- Prove the side effect, not only the HUD: after a purchase, show the points drop by the cost and show the effect (door gone and player standing on the blue back-room floor, weapon name and ammo changed, health bar reads 250).
- Name the run in your report and list the shot files.

## Cleanup

```sh
zv stop --run <name>
zv runs        # list runs and which still run
```

`stop` sends SIGTERM to the gamescope process group that `launch` started (and SIGKILL after 5 s), then marks the run stopped. It kills nothing else and never deletes `verify-artifacts/<run>/`. Never kill by process name (`pkill gamescope`, `pkill zombies`): the user may run their own copy. Delete old run directories by hand only when the user asks.

Stop the run after every failed attempt too, so no gamescope stays alive.

## Feature map

[features/README.md](features/README.md) indexes one recipe per user-facing feature. Read it before you drive a feature, and keep it current when the game changes.
