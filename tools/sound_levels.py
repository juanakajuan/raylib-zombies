from typing import NamedTuple

RATE = 44100
PEAK = 0.9
RELOAD_PEAK = 0.6
RELOAD_CEILING = 0.3


class Clip(NamedTuple):
    source: str
    keep_after_onset: float | None
    peak: float
    limiter_ceiling: float | None = None

    @property
    def finished_peak(self) -> float:
        return self.peak if self.limiter_ceiling is None else min(self.peak, self.limiter_ceiling)


CLIPS = {
    "fire_pistol": Clip("A_42P.wav", 0.7, PEAK),
    "fire_smg": Clip("G_31P.wav", 0.4, PEAK),
    "fire_shotgun": Clip("O_21P.wav", 0.9, PEAK),
    "reload_pistol": Clip("gunreload1.wav", None, RELOAD_PEAK, RELOAD_CEILING),
    "reload_smg": Clip("assaultriflereload1_0.wav", None, RELOAD_PEAK, RELOAD_CEILING),
    "reload_shotgun": Clip("shotguncock_0.wav", None, RELOAD_PEAK, RELOAD_CEILING),
    "growl_1": Clip("zombie-17.wav", None, PEAK),
    "growl_2": Clip("zombie-16.wav", None, PEAK),
    "growl_3": Clip("zombie-21.wav", None, PEAK),
    "attack_1": Clip("zombie-24.wav", None, PEAK),
    "attack_2": Clip("zombie-5.wav", None, PEAK),
    "attack_3": Clip("zombie-11.wav", None, PEAK),
}
