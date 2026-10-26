# Combat and rounds

The player shoots zombies with the left mouse button and earns points: 10 per hit, 60 per kill, 100 per headshot kill. Each round starts after a 5 s "Get ready" countdown and sends 6 + 3 × (round − 1) zombies. When all are dead, the round number goes up and a new countdown starts.

## Sub-features

- `combat-countdown` shows `ROUND 1` and `Get ready... 5` to `1` at the start, then `Zombies left: 6`.
- `combat-fire` takes one round from the magazine per click with the M1911 (`8 / 96` → `7 / 96`).
- `combat-reload` refills the magazine from the reserve with R and shows `RELOADING` for 1.4 s.
- `combat-points` adds points when a zombie is hit or killed, and lowers `Zombies left`.
- `combat-health-bars` shows a thin red bar above each living zombie that shrinks as it takes damage, and hides it behind walls. The fill is red so `find_zombies` still sees only the green skin.
- `combat-damage-numbers` floats a small white number up from the hit point for 0.8 s (yellow and larger for a headshot, orange for a kill). Shotgun pellets on one zombie merge into one number.
- `combat-round-advance` shows `ROUND 2` and a new countdown after the last zombie of a round dies.
- `combat-damage-regen` lowers the health bar when a zombie hits the player, and refills it 3 s after the last hit.

## How to get to it (user POV)

- Launch the game. Combat starts after the first countdown.
- Left mouse fires, R reloads, the mouse aims.

## Driving it with zv

Preconditions:

- A fresh game from `zv launch --run combat-1`; `zv doctor --run combat-1` prints `OK`.

- **Countdown.** Run `zv shot 01-countdown`. The banner reads `ROUND 1` with `Get ready...` and a number from 5 to 1. The HUD reads round `1`, points `500`, `M1911`, `8 / 96`, health `100`.
- **Fire.** Click three times at the wall. Run `zv click --count 3` then `zv shot 02-fired --crop weapon`. Ammo reads `5 / 96`.
- **Reload.** Press R. Run `zv key r && sleep 0.3 && zv shot 03-reloading --crop weapon && sleep 1.5 && zv shot 04-reloaded --crop weapon`. Shot 03 shows `RELOADING`. Shot 04 reads `8 / 93`.
- **Kill for points.** Wait for zombies, then hunt. Run `sleep 4 && zv zombies && zv hunt --seconds 15 && zv shot 05-after-kills --crop points`. `zv zombies` lists at least one zombie. Shot 05 shows points above 500 and `Zombies left` below 6.
- **Round advance.** Clear the round. Run `zv hunt --seconds 90 --until-clear && zv shot 06-round-2 --crop round`. Hunt ends with `stop=red banner`. The shot shows `ROUND 2` with a countdown and round number `2` in the corner.
- **Proof.** Stop with `zv stop --run combat-1`. Report shots 01 to 06 from `verify-artifacts/combat-1/`.

## Gotchas

- During the first 5 s no zombie exists. `zv zombies` prints `no zombies visible` and `hunt` only sweeps.
- `hunt` counts clicks, not hits, and does not read the HUD. Read the points from the shot.
- `hunt` without `--stay` steps back from close zombies, so the player position changes. Do not use the walking routes after it.
- Zombie spawns are random per run. A kill count or a point total is never an exact expectation.
- A zombie that is flashing red after a hit or falling down after death can still show as a target for a moment.
