# Vitality Brew perk

A blue perk machine stands in the back right corner of the front room. For 2500 points, E raises the player's max health from 100 to 250 and fills it. The perk can be bought once.

## Sub-features

- `perk-prompt` shows `Press E to drink Vitality Brew [2500]`, red when unaffordable.
- `perk-buy` takes 2500 points and sets the health bar to `250`.
- `perk-once` hides the prompt after the purchase.
- `perk-regen` regenerates health up to 250, not 100, after damage.

## How to get to it (user POV)

- From the start, walk right to the wall, then back toward the corner behind the start position, and press E.

## Driving it with zv

Preconditions:

- A fresh game from `zv launch --run perk-1`; `zv doctor --run perk-1` prints `OK`.

- **Reach the machine.** Run `zv key d --hold 2.0 && zv key s --hold 0.6 && zv shot 01-perk-locked --crop prompt` (use this position for the locked prompt only). The prompt reads `Press E to drink Vitality Brew [2500]` in red.
- **Earn 2500 points.** Run `zv hunt --seconds 120 --until-clear --stay --restore-view && zv shot 02-round-N --crop points` once per round. Repeat until shot `02-round-N` shows at least 2500 points. Rename the shot name per round (`02-round-2`, `03-round-3`).
- **Buy.** In the same shell command as the last hunt add `&& zv key e && sleep 0.3 && zv shot 10-perk-bought --crop health && zv shot 11-perk-points --crop points`. The health bar reads `250` and points are lower by exactly 2500.
- **Once only.** Run `zv shot 12-no-prompt --crop prompt`. No perk prompt shows.
- **Proof.** Stop with `zv stop --run perk-1`. Report the numbered shots from `verify-artifacts/perk-1/`.

## Gotchas

- 2500 points usually needs round 3 or later. The bot often dies in round 2 or 3. If it dies first, report the journey as failed with "bot died before 2500 points", and keep the shots.
- With `--stay` in the corner, zombies reach the player from one side only, but they still hit. Watch the health in each round shot.
- The machine is solid and lies behind the start position. The route ends with an `s` hold that pushes into it and stops against it. That is fine.
- Do not stand in the corner and hunt with `--stay`. In a 2026-10 pass the bot died in round 1 with 0 kills. Hunt in the open, and walk to the machine after the round clears. The locked prompt (red) was verified; the 2500 purchase was not reached by the bot.
