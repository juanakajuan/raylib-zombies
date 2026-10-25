# Game over and restart

Zombies hit for 40 damage. At 0 health the game dims the screen, lowers the camera to the floor over 0.5 s and shows a red `GAME OVER` title with `You survived N rounds` and `Kills: K`. After 1.5 s it adds `Press R to restart`, and R starts a fresh game: round 1, 500 points, M1911 only, door shut.

## Sub-features

- `gameover-screen` shows `GAME OVER`, rounds survived and kills when health reaches 0.
- `gameover-freeze` stops the player from moving, firing or buying on the game over screen.
- `gameover-restart` starts a fresh game with R once `Press R to restart` shows.

## How to get to it (user POV)

- Let zombies reach the player and stop fighting.
- Press R on the game over screen.

## Driving it with zv

Preconditions:

- A fresh game from `zv launch --run gameover-1`; `zv doctor --run gameover-1` prints `OK`.

- **Die.** Do nothing until zombies arrive and attack. Run `sleep 40 && zv shot 01-game-over`. The shot shows `GAME OVER`, `You survived 1 rounds`, `Kills: 0` and `Press R to restart`. If not, wait 10 s more and shoot again.
- **Frozen.** Run `zv click --count 2 && zv key w --hold 0.5 && zv shot 02-still-over --crop weapon`. Ammo still reads `8 / 96` and the screen is unchanged.
- **Restart.** Press R. Run `zv key r && sleep 0.5 && zv shot 03-restarted`. The banner reads `ROUND 1` with `Get ready...`, points `500`, health `100`, `8 / 96`.
- **Proof.** Stop with `zv stop --run gameover-1`. Report shots 01 to 03 from `verify-artifacts/gameover-1/`.

## Gotchas

- `You survived N rounds` counts the round the player died in, so a death in round 1 reads `1`.
- R does nothing during the first 1.5 s of the game over screen. Wait for `Press R to restart`.
- `hunt --until-clear` also stops on `GAME OVER`, because it watches for the red title. Read the shot to tell a cleared round from a death.
