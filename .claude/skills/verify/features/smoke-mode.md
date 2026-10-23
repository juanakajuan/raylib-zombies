# Smoke mode

`zombies --smoke` opens the real window, plays a scripted bot for 600 frames (about 10 s) with a fixed seed and 3000 starting points, buys the MP5K, shoots, and writes `smoke_1.png`, `smoke_2.png` and `smoke_3.png` to the working directory. It prints a `smoke: round=... kills=... points=...` summary and exits 0 only when all three screenshots exist. It is a developer check, not a player path.

## Sub-features

- `smoke-run` plays the scripted session in a 1280x720 window, also when `--fullscreen` is given.
- `smoke-shots` writes three PNG screenshots to the working directory.
- `smoke-exit` exits 0 when the three files exist, 1 otherwise.

## How to get to it (user POV)

- Run `./run.sh --smoke` or `./build/zombies --smoke` in a terminal.

## Driving it with zv

Preconditions:

- `zv build` succeeded.

- **Run it headless.** Launch through zv so the window stays off the desktop. Run `zv launch --run smoke-1 -- --smoke; sleep 14; zv runs; cat verify-artifacts/smoke-1/game.log | grep '^smoke:'`. `zv launch` may print `ready` or fail with `game exited during startup` if the 10 s bot ends first. `zv runs` shows `smoke-1 stopped`. The log has one `smoke: round=1 ...` line and no `expected 3 screenshots` error.
- **Screenshots.** Run `ls verify-artifacts/smoke-1/smoke_*.png`. Three files exist. Read them: shot 1 is the start view, shot 2 the MP5K prompt or purchase, shot 3 combat.
- **Proof.** Run `zv stop --run smoke-1`. Report the `smoke:` line and the three PNGs.

## Gotchas

- Smoke mode gives itself 3000 points and skips most of the countdown. Never use it as proof of a purchase or round feature. Use the matching feature recipe.
- The game cannot report its exit code through gamescope. Use the `smoke:` line and the PNG count instead.
- `./run.sh --smoke` without zv opens a real window on the user's desktop and grabs nothing, but it does take focus for 10 s.
