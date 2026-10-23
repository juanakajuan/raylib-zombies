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
- **Earn points and buy.** In one shell command run `zv hunt --seconds 90 --until-clear --stay --restore-view && zv shot 03-round-cleared --crop points && zv key e && sleep 0.3 && zv shot 04-door-bought --crop points`. Hunt ends with `stop=red banner`. Shot 03 shows `ROUND 2`, the white prompt and at least 750 points. Shot 04 shows points lower by exactly 750 and the back room (blue tiled floor, far wall) where the door was.
- **Enter the back room.** Walk forward. Run `zv key w --hold 2.0` then `zv shot 05-in-back-room`. The floor under the view is blue tiled, and no door prompt shows.
- **Proof.** Stop with `zv stop --run door-1`. Report shots 01 to 05 and `actions.log` from `verify-artifacts/door-1/`.

## Gotchas

- The hunt bot must not step back here (`--stay`): a step back can move the player out of the 3 m prompt radius.
- `--restore-view` turns back to face the door. Without it the purchase prompt can still show, but shots 04 and 05 do not look through the doorway.
- If shot 03 shows `GAME OVER` instead of `ROUND 2`, the bot died. Stop, launch a new run and try again. Do not report the purchase as verified.
- With `--stay` the zombies reach the player. Health can drop near 0 before the round clears; the purchase is still valid if the shot shows `ROUND 2`.
- Round 1 usually ends with 1100 to 1300 points. The exact value depends on hits and headshots.
