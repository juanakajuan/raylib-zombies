#ifndef CONTENT_H
#define CONTENT_H

/* The one list of weapons and perks. Every enum and table for them expands from these rows, so a row is
 * either whole or the build fails. A module defines a row macro, expands the list, and ignores the columns
 * it does not use. Synth names in CONTENT_SOUND resolve only in audio.c, the one module that reads them. */

#define CONTENT_RGB(r, g, b) {r, g, b, 255}
/* seconds, volume, synth function, file: the SoundDef fields in audio.c */
#define CONTENT_SOUND(seconds, volume, synth, file) {seconds, volume, synth, {file}}

/* X(id, name, damage, fire_interval, automatic, mag_size, reserve_max, reload_time, pellets, spread, wall_cost,
 *   view_length, panel, fire_sound, reload_sound)
 * id names WEAPON_<id>. view_length and panel are the first-person model. */
#define WEAPON_LIST(X) \
    X(PISTOL, "M1911", 40.0f, 0.28f, false, 8, 96, 1.4f, 1, 0.0f, 0, \
      0.12f, CONTENT_RGB(150, 150, 150), \
      CONTENT_SOUND(0.25f, 0.8f, FirePistol, "fire_pistol.wav"), CONTENT_SOUND(0.95f, 0.6f, Reload, "reload_pistol.wav")) \
    X(SMG, "MP5K", 30.0f, 0.085f, true, 32, 192, 1.8f, 1, 0.012f, 1000, \
      0.18f, CONTENT_RGB(226, 140, 40), \
      CONTENT_SOUND(0.12f, 0.6f, FireSmg, "fire_smg.wav"), CONTENT_SOUND(0.95f, 0.6f, Reload, "reload_smg.wav")) \
    X(SHOTGUN, "Shotgun", 26.0f, 0.8f, false, 6, 54, 2.4f, 6, 0.055f, 1500, \
      0.22f, CONTENT_RGB(196, 52, 52), \
      CONTENT_SOUND(0.6f, 1.0f, FireShotgun, "fire_shotgun.wav"), CONTENT_SOUND(0.95f, 0.6f, Reload, "reload_shotgun.wav"))

/* X(id, name, cost, max_health, reload_scale, body, glow)
 * id names PERK_<id>. max_health 0 leaves health alone. reload_scale multiplies every reload time. */
#define PERK_LIST(X) \
    X(VITALITY, "Vitality Brew", 2500, PERK_MAX_HEALTH, 1.0f, CONTENT_RGB(46, 118, 214), CONTENT_RGB(255, 232, 150)) \
    X(QUICK_HANDS, "Quick Hands", 3000, 0.0f, QUICK_HANDS_RELOAD_SCALE, CONTENT_RGB(222, 108, 36), CONTENT_RGB(150, 255, 160))

#endif
