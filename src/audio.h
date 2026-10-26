#ifndef AUDIO_H
#define AUDIO_H

#include "game.h"

/* True for --mute or ZOMBIES_MUTE set to a value other than empty or 0. */
bool AudioMuted(int argc, char **argv);
/* Without an audio device the calls below do nothing. When muted, the device is never opened. */
void AudioInit(bool muted);
/* Logs each sound at LOG_DEBUG, muted or not. */
void AudioPlayEvent(const GameEvent *event);
void AudioShutdown(void);

#endif
