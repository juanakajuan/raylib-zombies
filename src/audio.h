#ifndef AUDIO_H
#define AUDIO_H

#include "game.h"

/* Without an audio device the calls below do nothing. */
void AudioInit(void);
void AudioPlayEvent(const GameEvent *event);
void AudioShutdown(void);

#endif
