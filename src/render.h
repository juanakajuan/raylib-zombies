#ifndef RENDER_H
#define RENDER_H

#include "game.h"
#include "settings.h"

void GameDraw(const GameState *g, float fov_degrees);
void DrawPauseOverlay(const Settings *settings, int selected);

#endif
