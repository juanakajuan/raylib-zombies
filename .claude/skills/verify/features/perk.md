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

- **Reach the machine.** Run `zv key d --hold 2.0 && zv key s --hold 0.6 && zv shot 01-perk-locked --crop prompt`. The prompt reads `Press E to drink Vitality Brew [2500]` in red.
- **Earn 2500 points.** Run `zv hunt --seconds 120 --until-clear --stay --restore-view && zv shot 02-round-N --crop points` once per round. Repeat until shot `02-round-N` shows at least 2500 points. Rename the shot name per round (`02-round-2`, `03-round-3`).
- **Buy.** In the same shell command as the last hunt add `&& zv key e && sleep 0.3 && zv shot 10-perk-bought --crop health && zv shot 11-perk-points --crop points`. The health bar reads `250` and points are lower by exactly 2500.
- **Once only.** Run `zv shot 12-no-prompt --crop prompt`. No perk prompt shows.
- **Proof.** Stop with `zv stop --run perk-1`. Report the numbered shots from `verify-artifacts/perk-1/`.

## Gotchas

- 2500 points usually needs round 3 or later. The bot often dies in round 2 or 3. If it dies first, report the journey as failed with "bot died before 2500 points", and keep the shots.
- With `--stay` in the corner, zombies reach the player from one side only, but they still hit. Watch the health in each round shot.
- The machine is solid. Walking into it stops the player, which is why the route ends with a step back (`s`) instead of a step into it.
