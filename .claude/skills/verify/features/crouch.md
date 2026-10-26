# Jump, crouch and the low slab

Space jumps and holding Ctrl or C crouches. Crouching lowers the player's whole body, not only the view. A rust-brown slab hangs low against the west wall of the front room, from x -10 to -6.5 and z 2.8 to 4.8, with its underside 1.3 m above the floor. A standing player is stopped at its edge. A crouching player walks under it.

## Sub-features

- `jump` lifts the view for about 0.7 s and lands. Space does nothing in the air or while crouched.
- `crouch-view` eases the view down to 1.0 m at 6 m/s and back to 1.7 m on release, and slows walking to 2.2 m/s.
- `slab-blocks-standing` stops a standing player at the slab's +Z edge, at z about 5.2.
- `slab-crouch-pass` lets `c+w` pass under the slab.
- `slab-stays-low` keeps the view low and the walk slow after `c` is released under the slab. Jump does nothing there. The view rises once the player leaves.
- `slab-stops-bullets` stops shots at the slab.

## How to get to it (user POV)

- Press Space to jump and hold Ctrl or C to crouch anywhere.
- From the start, strafe left to the west wall side of the room, walk toward the door (-Z) into the slab, then crouch and walk under it.

## Driving it with zv

Preconditions:

- A fresh game from `zv launch --run crouch-1`; `zv doctor --run crouch-1` prints `OK`.

- **Line up.** Run `zv key a --hold 1.83 && zv shot 01-before-slab`. The player is at x about -8.2, facing -Z. The slab shows as a brown beam on the left, ahead.
- **Standing is blocked.** Run `zv key w --hold 1.6 && zv shot 02-blocked`. The player stops at z about 5.2, right at the slab edge. A longer hold changes nothing.
- **Crouch passes.** Run `zv key c+w --hold 0.75 && zv shot 03-under`. The player is under the slab at z about 3.8, the view is low, and the beam is above the view. `Control_L+w` works the same.
- **Release under it.** Run `zv key w --hold 0.3 && zv shot 04-released`. The view stays low and the walk is slow. Run `zv key space --hold 0.2 && zv shot 05-no-jump`. Nothing lifts.
- **Leave.** Run `zv key w --hold 0.7 && sleep 0.6 && zv shot 06-out`. The player is past the slab at z about 2.3 and the view is back to standing height.
- **Jump.** Run `zv key space --hold 0.1 && zv shot 07-jump` and compare the view height with shot 06. A tap lifts the view for about 0.7 s, so take the shot straight after the key.
- **Proof.** Stop with `zv stop --run crouch-1`. Report the numbered shots from `verify-artifacts/crouch-1/`.

## Gotchas

- The hold times come from the headless simulation at a fixed 60 Hz step (a 1.83 s `a` hold, a 1.07 s `w` hold to the slab edge, 0.72 s of `c+w` to z 3.8). Real key holds jitter by a few frames, so check the shots and not the exact numbers.
- Walls stop the player, so a long `w` hold is safe. A short `c+w` hold stops before the slab, so use 0.75 s or more.
- Zombies pass under the slab. Do the route during the first intermission, before the round starts, so they do not push or hit the player.
- The crouch view eases over about 0.1 s to reach 1.2 m, the height that clears the slab. Pressing `c+w` together is enough.
