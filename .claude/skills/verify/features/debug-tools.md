# Debug tools

Started with `--debug`, F1 opens a `DEBUG` overlay that freezes the game, frees the mouse and lists cheat commands: `+5000 points`, one `Give <weapon>` row per weapon, one `Give <perk>` row per perk, `Refill ammo`, `Full health`, `God mode: ON/OFF`, `Open all doors` and `Skip round`. Up/Down move the selection (it wraps), hovering a row selects it, and Enter or a click on a row applies it. F1 or Esc closes the overlay. While god mode is on, `GOD MODE` shows in the top right of the HUD. Without `--debug`, F1 does nothing.

## Sub-features

- `debug-toggle` shows and hides the `DEBUG` overlay with F1, and closes it with Esc. F1 does nothing while the pause overlay is open.
- `debug-points` adds 5000 points.
- `debug-give` gives a weapon or perk as if bought: the weapon becomes active with full ammo, Vitality Brew sets max health to 250.
- `debug-state` refills ammo, restores health, opens the back-room door and toggles god mode.
- `debug-skip-round` ends the current round without kills or points, or starts the next round from the intermission.

## How to get to it (user POV)

- Start with `./run.sh --debug` (or `./build/zombies --debug`), then press F1 during play.

## Driving it with zv

Preconditions:

- A fresh game from `zv launch --run debug-1 -- --debug`. `zv doctor --run debug-1` prints `OK`.

- **Open.** Run `zv key F1 && zv shot 01-menu`. The shot shows `DEBUG` with `+5000 points` highlighted.
- **Points.** Run `zv key Return && zv key F1 && sleep 0.3 && zv shot 02-points --crop points`. Points read 5500 (500 plus 5000).
- **Give a weapon.** Run `zv key F1 && zv key Down && zv key Down && zv key Return && zv key F1 && zv shot 03-smg`. The HUD shows MP5K with a full `32 / 192` magazine.
- **Close with Esc.** Run `zv key F1 && zv key Escape && zv shot 04-closed`. The overlay is gone.
- **Proof.** Report shots 01 to 04 from `verify-artifacts/debug-1/`.

## Gotchas

- Row order is: points, 3 weapons, 2 perks, refill ammo, full health, god mode, open doors, skip round. Count Down presses from the top row, since the selection starts at row 0 and is kept between openings.
- Esc from the debug overlay closes it and does not open the pause menu.
- The menu is a cheat. Do not use it as proof of a purchase, a door or a perk the user buys. Use it only to reach a state quickly.
