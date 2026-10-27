#ifndef NAV_H
#define NAV_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#define NAV_CELL_SIZE 0.5f
#define NAV_MIN_X (-10.0f)
#define NAV_MIN_Z (-16.0f)
#define NAV_COLS 40
#define NAV_ROWS 60
#define NAV_CELLS (NAV_COLS * NAV_ROWS)
#define NAV_UNREACHED UINT16_MAX

typedef struct NavField {
    bool ready;
    uint32_t walkable_open_zones;
    int goal;
    bool walkable[NAV_CELLS];
    uint16_t cost[NAV_CELLS]; /* path cost to the goal, 10 per straight step and 14 per diagonal */
} NavField;

int NavCellAt(Vector3 pos);
Vector3 NavCellCenter(int cell);
void NavFlood(NavField *f, int goal);
/* Unit XZ direction from a zombie at from toward the player at target, around every solid. */
Vector3 NavDirection(const NavField *f, Vector3 from, Vector3 target);

#endif
