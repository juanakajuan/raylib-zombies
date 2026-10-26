# Wall-buy weapons

Two weapon panels hang on the front room's side walls: the MP5K (orange panel, left wall, 1000 points, automatic) and the shotgun (red panel, right wall, 1500 points, six pellets). The player carries at most two weapons. A purchase fills the second slot, or replaces the weapon in hand when both slots are full. Number keys 1 and 2 select a slot, and the mouse wheel selects the next (down) or previous (up) weapon. At a panel for a weapon the player already owns, E refills its ammo for the same price.

## Sub-features

- `wallbuy-prompt` shows `Press E to buy MP5K [1000]` or `Press E to buy Shotgun [1500]` near the panel, red when unaffordable.
- `wallbuy-buy` takes the cost and puts the new weapon in hand with full ammo (`MP5K 32 / 192`, `Shotgun 6 / 54`).
- `wallbuy-swap` switches weapons with keys 1 and 2 and with the mouse wheel, and shows the other weapon as `[<slot>] <name>` above the weapon name.
- `wallbuy-refill` shows `Press E to refill ammo for <name> [cost]` at the panel of an owned weapon that is not full.
- `wallbuy-auto` fires the MP5K continuously while the left button is held.

## How to get to it (user POV)

- Walk to the orange panel on the left wall or the red panel on the right wall and press E.
- Press 1 or 2, or scroll the mouse wheel, to switch weapons.

## Driving it with zv

Preconditions:

- A fresh game from `zv launch --run wallbuy-1`; `zv doctor --run wallbuy-1` prints `OK`.

- **Reach the MP5K.** In a throwaway first run, run `zv key a --hold 2.0 && zv key w --hold 0.65 && zv shot 01-mp5k-locked --crop prompt`. The prompt reads `Press E to buy MP5K [1000]` in red.
- **Earn and buy.** Hunt in the open first, then walk to the panel. In one shell command run `zv hunt --seconds 100 --until-clear --restore-view && zv shot 02-round-cleared --crop points && zv key a --hold 2.0 && zv key w --hold 0.65 && zv key e && sleep 0.3 && zv shot 03-mp5k-bought --crop points && zv shot 04-mp5k-hud --crop weapon`. Shot 02 shows at least 1000 points. Shot 03 shows points lower by exactly 1000. Shot 04 shows `MP5K`, `32 / 192` and `[1] M1911`.
- **Automatic fire.** Hold fire. Run `zv click --hold 1.0 && zv shot 05-mp5k-auto --crop weapon`. The magazine drops by about 11 (one round per 0.085 s), not by 1.
- **Refill.** At the same panel run `zv shot 06-refill-prompt --crop prompt`. The prompt reads `Press E to refill ammo for MP5K [1000]`.
- **Swap.** Press 1, then scroll. Run `zv key 1 && sleep 0.4 && zv shot 07-swapped --crop weapon && zv wheel down && sleep 0.4 && zv shot 07b-wheel --crop weapon`. Shot 07 shows `M1911` and `[2] MP5K`. Shot 07b shows `MP5K` and `[1] M1911`.
- **Shotgun.** The shotgun needs 1500 points, which usually means surviving into round 2 or 3. From a fresh run: `zv key d --hold 2.0 && zv key w --hold 0.45`, then hunt with `--stay --restore-view --until-clear` once per round and read the points after each. Buy with `zv key e` when the shot shows at least 1500. The HUD then shows `Shotgun` and `6 / 54`.
- **Proof.** Stop with `zv stop --run wallbuy-1`. Report the numbered shots from `verify-artifacts/wallbuy-1/`.

## Gotchas

- The prompt shows only within 2.6 m of a panel. Do not hunt while standing at a panel with `--stay`: the bot died in round 1 at the door in 3 of 3 attempts. Hunt in the open and walk to the panel after the round clears (verified: 1160 to 160 points, `32 / 192`).
- After `--until-clear` the intermission lasts 5 s. The walk to the panel takes about 2.7 s, so keep it in the same shell command as the hunt.
- A full owned weapon shows no prompt at its panel. Fire a few rounds first to see the refill prompt.
- `hunt` assumes the M1911 (`--mag 8`, one click per shot). With the MP5K in hand pass `--mag 32 --fire-interval 0.1`.
- The bot often dies in round 2 or 3. A shotgun journey that dies before 1500 points is a failed run, not a pass.
