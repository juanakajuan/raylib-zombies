#ifndef RENDER_H
#define RENDER_H

#include "game.h"
#include "settings.h"

void GameDraw(const GameState *g, float fov_degrees);
/* The pause menu lists every setting, then one row for the window mode. */
#define PAUSE_ROW_DISPLAY SETTING_COUNT
#define PAUSE_ROW_COUNT (SETTING_COUNT + 1)

void DrawPauseOverlay(const Settings *settings, int selected, bool fullscreen);
Rectangle PauseRowBounds(int row);
Rectangle PauseBarBounds(int row);
void DrawDebugOverlay(const GameState *g, int selected);
Rectangle DebugRowBounds(int row);
/* The mouse position in the layout units that the HUD and pause bounds use. */
Vector2 UiMousePosition(void);

#endif
