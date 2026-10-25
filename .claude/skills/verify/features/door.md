# Open the door

The front room has one door in its far wall. A player who stands near it with at least 750 points can press E to open it. The door disappears, 750 points are taken, and the back room (blue tiled floor) and its zombie spawns open.

## Sub-features

- `door-prompt` shows `Press E to open the door [750]`, red when the player has under 750 points, white when it is affordable.
- `door-refuse` keeps the door shut and the points unchanged when E is pressed with under 750 points.
- `door-buy` removes the door and takes exactly 750 points.
- `door-enter` lets the player walk through into the back room.

## How to get to it (user POV)

- From the start position, walk straight forward to the door and press E.

## Driving it with zv

Preconditions:

- A fresh game from `zv launch --run door-1`; `zv doctor --run door-1` prints `OK`.

- **Reach the door.** Walk forward. Run `zv key w --hold 2.5` then `zv shot 01-door-locked --crop prompt`. The prompt reads `Press E to open the door [750]` in red and the door fills the view.
- **Refuse when poor.** Press E with 500 points. Run `zv key e` then `zv shot 02-refused --crop points`. Points still read `500` and the door is still there.
- **Earn points and buy.** Stop `door-1` and launch a fresh `door-2`. Hunt in the open first, then walk to the door. In one shell command run `zv hunt --seconds 100 --until-clear --restore-view && zv shot 03-round-cleared --crop points && zv key w --hold 2.5 && zv shot 03b-prompt --crop prompt && zv key e && sleep 0.3 && zv shot 04-door-bought --crop points`. Hunt ends with `stop=red banner`. Shot 03 shows `ROUND 2` and at least 750 points. Shot 03b shows the white prompt. Shot 04 shows points lower by exactly 750 and the back room (blue tiled floor, far wall) where the door was. The buy also works after round 2 has started; there is no phase gate.
- **Enter the back room.** Walk forward. Run `zv key w --hold 2.0` then `zv shot 05-in-back-room`. The floor under the view is blue tiled, and no door prompt shows.
- **Proof.** Stop with `zv stop --run door-2`. Report shots 01 and 02 from `verify-artifacts/door-1/`, shots 03 to 05 from `verify-artifacts/door-2/`, and both `actions.log` files.

## Gotchas

- Do not hunt from the door with `--stay`. In a 2026-10 pass the bot died in round 1 in 3 of 3 attempts when it stood at the door (840, 1070 and 840 points, never the round clear). Hunting in the open from the start position and then walking to the door cleared round 1 with 1160 points.
- `--restore-view` turns the view back to the start direction, so the walk to the door in the buy command is straight ahead.
- If shot 03 shows `GAME OVER` instead of `ROUND 2`, the bot died. Stop, launch a new run and try again. Do not report the purchase as verified.
- Round 1 usually ends with 1100 to 1300 points (about 114 per kill measured: 1160 in the verified run). The exact value depends on hits and headshots.
