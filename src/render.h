#ifndef RENDER_H
#define RENDER_H

#include "game.h"
#include "settings.h"

void GameDraw(const GameState *g, float fov_degrees);
void DrawPauseOverlay(const Settings *settings, int selected);
Rectangle PauseRowBounds(int row);
Rectangle PauseBarBounds(int row);
/* The mouse position in the layout units that the HUD and pause bounds use. */
Vector2 UiMousePosition(void);

#endif
