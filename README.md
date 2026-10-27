# raylib-zombies

A small first-person zombie survival proof of concept in C11 with raylib 5.5.
Survive rounds of zombies, earn points by shooting them, and spend points on a door, wall-buy weapons and perks.

## Build and run

`./run.sh` builds the game if needed and then starts it. It passes its arguments to the game, as in `./run.sh --fullscreen`.

The game starts in fullscreen at the monitor's native resolution. Use `--windowed` to start in a 1280x720 window on the primary monitor. `--smoke` runs a scripted session that saves three screenshots and always uses the window. `--mute`, or the environment variable `ZOMBIES_MUTE=1`, starts the game with no audio device.

To do the steps by hand:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/zombies
```

The first configure on a machine clones raylib 5.5 into `${XDG_CACHE_HOME:-~/.cache}/raylib-zombies/raylib-5.5`, so it needs network access.
Later configures, in any worktree, use that copy. To use a different copy, set `-DFETCHCONTENT_SOURCE_DIR_RAYLIB=<path>`.
On Linux it also needs the X11 and OpenGL development headers.

## Controls

Move the mouse to look around. In the pause menu, move the pointer onto a setting row to select it, and drag its bar to change it. `src/input.c` holds the key bindings, and the tables below come from them. `input_test` fails when the tables are stale. Run `build/input_test --print-controls` to print new tables.

<!-- controls:begin -->

**Playing**

| Key | Action |
| --- | --- |
| F11 or Alt+Enter | fullscreen |
| Esc | pause and free the mouse |
| F1 | debug menu (with `--debug`) |
| W A S D | move |
| Left mouse | fire |
| R | reload (restart on the game over screen) |
| E | buy or interact |
| 1, 2 or mouse wheel | select weapon |
| Shift | sprint |
| Space | jump |
| Ctrl / C | crouch (hold) |

**Paused**

| Key | Action |
| --- | --- |
| F11 or Alt+Enter | fullscreen |
| Esc | resume |
| Q | quit |
| Up/Down | select |
| Left/Right or mouse | adjust |

**Debug menu**

| Key | Action |
| --- | --- |
| F11 or Alt+Enter | fullscreen |
| F1 or Esc | close |
| Up/Down | select |
| Enter or click | apply |

<!-- controls:end -->

## Settings

The pause menu lists volume (0 to 1), mouse sensitivity (0.2x to 3x) and field of view (60 to 110 degrees). Changes apply at once and are saved to `settings.cfg` in the working directory when you resume or quit. The file is plain `key=value` text. Unknown keys are ignored and out-of-range values are clamped. `--smoke` ignores the file and uses the defaults.

## Debug tools

Start with `--debug` to enable the F1 menu. It freezes the game and lists cheats: +5000 points, give any weapon or perk, refill ammo, full health, god mode, open all doors and skip round. Use Up / Down and Enter, or the mouse. F1 or Esc closes it. `--debug` also writes a log line for each sound event, also when muted.

## Features

- Rounds with an intermission countdown. Zombie count, health and speed grow each round, with sprinters from round 4.
- Hitscan shooting with headshots. Points: 10 per hit, 60 per kill, 100 per headshot kill.
- A health bar above every living zombie, and floating damage numbers when you shoot one (yellow for headshots, orange for kills).
- Three weapons from a data table: M1911 pistol, MP5K (1000 points, automatic) and shotgun (1500 points, multi-pellet). You carry two.
- A 750 point door that opens the back room and its zombie spawns.
- Two perks: Vitality Brew (2500 points) raises max health from 100 to 250. Quick Hands (3000 points, back room) halves reload time.
- A low slab in the front room that you must crouch to pass under, so crouching changes your body and not only your view.
- Health regenerates after 3 seconds without damage. Dying shows the game over screen, and R restarts.
- Sound effects for each weapon, reload, hits and kills, zombie growls and attacks, player damage, round start and end, and purchases (with a buzz when you lack the points).

## Tests and smoke run

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/zombies --smoke
```

`zombies_test` drives the simulation headlessly with scripted input.
`invariant_test` drives the simulation with seeded random input and checks the sim invariants after every step. A failure prints the seed and the step. Run `./build/invariant_test <seeds> <steps>` to try more seeds than the default 8 seeds of 50,000 steps.
`--smoke` opens the real window, plays a scripted bot for about 10 seconds and writes `smoke_1.png`, `smoke_2.png` and `smoke_3.png` to the working directory.

`./build/zombies_bench` measures the simulation cost at 240 Hz with 24 chasing zombies, over 7 runs. For a player that stands still and for a player that walks a circle, it prints the median microseconds per `GameUpdate` step, the 99.9th percentile step, the median and the maximum of the worst step in each run, and the number of zombie flow-field floods. Then it prints the time of one flood and of a full field rebuild, and the median microseconds per `RouteInput` call for a busy frame of play. It is not part of `ctest`. Use a Release build for the measurement.

## Sound sources and licenses

Gun, reload and zombie sounds are clips from the files below, cut and converted by `tools/prepare_sounds.py` into `assets/`. Every other sound (hit, kill, headshot kill, player damage, round start and end, purchase, denied) is synthesized in `src/audio.c`, is original to this project and falls under its MIT license.
A missing clip falls back to a synthesized sound. Without an audio device the game runs silently.

| Clips | Source | Author | License |
| --- | --- | --- | --- |
| `fire_pistol`, `fire_smg`, `fire_shotgun` | [The Free Firearm Sound Library](https://opengameart.org/content/the-free-firearm-sound-library) (Colt 1911 `A_42P`, Carl Gustav M45 `G_31P`, Benelli Nova `O_21P`) | Ben Jaszczak, Brian Nelson, Kevin Heras, Matthew Nanney | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `reload_pistol`, `reload_smg`, `reload_shotgun` | [gun reload sounds](https://opengameart.org/content/gun-reload-sounds) | SpringySpringo | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `growl_1` to `growl_3`, `attack_1` to `attack_3` | [Zombies Sound Pack](https://opengameart.org/content/zombies-sound-pack) (`zombie-17`, `16`, `21`, `24`, `5`, `11`) | artisticdude | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |

## Layout

- `src/game.h` and `src/game.c` hold the simulation and the static level tables. They make no window, input or audio calls. `GameUpdate` writes sound events to a fixed-size queue in `GameState`.
- `src/settings.h` and `src/settings.c` hold the settings table (name, range, step, default) that drives clamping, parsing, saving and the pause menu rows.
- `src/render.c` draws the scene and HUD from a `const GameState *`.
- `src/audio.c` turns those events into sounds. `assets/` holds the clips and is copied next to the executable at build time. `src/main.c` plays them after each sim step.
- `src/main.c` reads input, runs the fixed-step loop and implements `--smoke`.
- `tests/game_test.c` is the headless core-loop test.
- `tests/invariant_test.c` is the random-input invariant test.

## License

MIT. See [LICENSE](LICENSE).
