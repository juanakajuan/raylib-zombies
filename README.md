# raylib-zombies

A small first-person zombie survival proof of concept in C11 with raylib 5.5.
Survive rounds of zombies, earn points by shooting them, and spend points on a door, wall-buy weapons and a perk.

## Build and run

`./run.sh` builds the game if needed and then starts it. It passes its arguments to the game, as in `./run.sh --fullscreen`.

The game opens in a 1280x720 window on the primary monitor. Use `--fullscreen` to start in fullscreen at the monitor's native resolution. `--smoke` runs a scripted session that saves three screenshots and always uses the window.

To do the steps by hand:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/zombies
```

The first configure downloads raylib 5.5 through CMake `FetchContent`, so it needs network access.
On Linux it also needs the X11 and OpenGL development headers.

## Controls

| Key | Action |
| --- | --- |
| W A S D | Move |
| Mouse | Look |
| Left mouse | Fire |
| R | Reload (restart on the game over screen) |
| E | Buy or interact |
| Q | Swap weapon |
| Shift | Sprint |
| Esc | Pause and free the mouse. Esc again resumes, Q quits while paused |
| F11 or Alt+Enter | Switch between fullscreen and windowed |

## Features

- Rounds with an intermission countdown. Zombie count, health and speed grow each round, with sprinters from round 4.
- Hitscan shooting with headshots. Points: 10 per hit, 60 per kill, 100 per headshot kill.
- Three weapons from a data table: M1911 pistol, MP5K (1000 points, automatic) and shotgun (1500 points, multi-pellet). You carry two.
- A 750 point door that opens the back room and its zombie spawns.
- A 2500 point perk that raises max health from 100 to 250.
- Health regenerates after 3 seconds without damage. Dying shows the game over screen, and R restarts.
- Sound effects for each weapon, reload, hits and kills, zombie growls and attacks, player damage, round start and end, and purchases (with a buzz when you lack the points).

## Tests and smoke run

```sh
ctest --test-dir build --output-on-failure
./build/zombies --smoke
```

`zombies_test` drives the simulation headlessly with scripted input.
`--smoke` opens the real window, plays a scripted bot for about 10 seconds and writes `smoke_1.png`, `smoke_2.png` and `smoke_3.png` to the working directory.

## Sound sources and licenses

Gun, reload and zombie sounds are clips from the files below, cut and converted by `tools/prepare_sounds.py` into `assets/`. Every other sound (hit, kill, headshot kill, player damage, round start and end, purchase, denied) is synthesized in `src/audio.c`, is original to this project and falls under its MIT license.
A missing clip falls back to a synthesized sound. Without an audio device the game runs silently.

| Clips | Source | Author | License |
| --- | --- | --- | --- |
| `fire_pistol`, `fire_smg`, `fire_shotgun` | [gunshot sounds](https://opengameart.org/content/gunshot-sounds) (`cz.wav`, `sks.wav`, `shotty.wav`) | Vincent Sevedge, 2009 | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| `reload_pistol`, `reload_smg`, `reload_shotgun` | [gun reload sounds](https://opengameart.org/content/gun-reload-sounds) | SpringySpringo | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `growl_1` to `growl_3`, `attack_1` to `attack_3` | [Zombies Sound Pack](https://opengameart.org/content/zombies-sound-pack) (`zombie-17`, `16`, `21`, `24`, `5`, `11`) | artisticdude | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |

The gunshot page lists CC0, but the `creativecommons.txt` inside its zip says CC BY 3.0, so the table follows the zip and credits the author.

## Layout

- `src/game.h` and `src/game.c` hold the simulation and the static level tables. They make no window, input or audio calls. `GameUpdate` writes sound events to a fixed-size queue in `GameState`.
- `src/render.c` draws the scene and HUD from a `const GameState *`.
- `src/audio.c` turns those events into sounds. `assets/` holds the clips and is copied next to the executable at build time. `src/main.c` plays them after each sim step.
- `src/main.c` reads input, runs the fixed-step loop and implements `--smoke`.
- `tests/game_test.c` is the headless core-loop test.

## License

MIT. See [LICENSE](LICENSE).
