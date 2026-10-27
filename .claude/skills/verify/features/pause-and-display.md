# Pause and display mode

Esc pauses the game: the simulation stops, the mouse is freed and a `PAUSED` overlay lists the volume, sensitivity and fov settings (Up/Down select, Left/Right adjust, or hover and click or drag a bar with the mouse) and a `window mode` row that shows `fullscreen` or `windowed` above the hint `F11 or Alt+Enter fullscreen, Esc resume, Q quit, Up/Down select, Left/Right or mouse adjust`. That hint comes from the `BINDINGS` table in `src/input.c`. Settings are saved to `settings.cfg` in the working directory when the game resumes or quits. Esc again resumes and Q while paused quits. F11 or Alt+Enter switches between fullscreen at the monitor's resolution and a 1280x720 window centred on the primary monitor. The game starts in fullscreen. The `--fullscreen` and `--windowed` flags pick the start mode.

## Sub-features

- `pause-toggle` shows and hides the `PAUSED` overlay with Esc.
- `pause-freezes` stops the countdown, zombies and regeneration while paused.
- `pause-settings` changes the selected setting with Left/Right or by clicking and dragging its bar, moves the selection with Up/Down, and writes `settings.cfg` on resume. A fresh launch reads it back.
- `pause-quit` exits the game with Q while paused. Q while not paused does nothing.
- `display-menu` switches fullscreen and windowed from the `window mode` row with Left/Right or a click. It is the fourth row, so press Down three times after Esc.
- `display-toggle` switches fullscreen and windowed with F11 and with Alt+Enter, also while paused.
- `display-flags` starts fullscreen with `--fullscreen` or no flag, and windowed with `--windowed`.
- `display-bad-flag` prints `usage: ... [--fullscreen | --windowed] [--smoke] [--debug] [--mute]` and exits 2 on an unknown flag.

## How to get to it (user POV)

- Press Esc during play, then Esc, F11 or Q.
- Press F11, or Alt+Enter, at any time.
- Start with `./run.sh` or `./run.sh --fullscreen` for fullscreen, or `./run.sh --windowed` for a window.

## Driving it with zv

Preconditions:

- A fresh game from `zv launch --run display-1 --output 1920x1080`. The larger headless monitor makes fullscreen (1920x1080) differ from the window (1280x720).
- `zv doctor --run display-1` prints `OK` and `window 1920x1080 (fullscreen)`, because the game starts in fullscreen.

- **Pause.** Press Esc. Run `zv key Escape && zv shot 01-paused && sleep 2 && zv shot 02-still-paused`. Both shots show `PAUSED` and the same `Get ready...` number, because the countdown is frozen.
- **Resume.** Press Esc. Run `zv key Escape && sleep 1.2 && zv shot 03-resumed`. The overlay is gone and the countdown has moved on.
- **F11.** Run `zv window && zv key F11 && sleep 0.5 && zv window && zv shot 04-windowed`. The first line reads `mode fullscreen` (1920x1080), the second `mode windowed` (1280x720).
- **Alt+Enter.** Run `zv key Alt_L+Return && sleep 0.5 && zv window`. It reads `mode fullscreen` again.
- **Quit.** Pause and press Q. Run `zv key Escape && zv key q; sleep 1; zv doctor`. The `zv key q` call may print `the X display closed`. Doctor reports `PROBLEM game pid ... is not running`, which is the expected result here.
- **Start flags.** Run `zv stop --run display-1`, then `zv launch --run display-2 --output 1920x1080 -- --windowed && zv window`. It reads `mode windowed`. Stop it with `zv stop --run display-2`.
- **Bad flag.** Run `./build/zombies --mute --bogus; echo "exit=$?"`. It prints the usage line and `exit=2` without opening a window.
- **Proof.** Report shots 01 to 04 (shot 04 shows the 1280x720 window), the `zv window` lines (also in `actions.log`) from `verify-artifacts/display-1/` and `verify-artifacts/display-2/`.

## Gotchas

- With the default `--output 1280x720` fullscreen and windowed are the same size, and `zv window` cannot tell them apart. Use `--output 1920x1080`.
- The headless output has one monitor. The "centred on the primary monitor" part of windowed mode is not provable here; check it on a real desktop.
- After resume or a display switch the game drops mouse motion for 2 frames. Wait at least 0.1 s before `zv look`.
- When the game quits, gamescope exits too. Every later `zv` input command fails with `the X display closed`.
