#ifndef LEGACY_ROUTING_H
#define LEGACY_ROUTING_H

#include "input.h"

typedef Routed (*RouteFn)(Overlay overlay, bool debug, const KeySnapshot *keys, float look_scale);

typedef enum LegacyEra {
    ERA_PRE_E6379C6, /* Esc was raylib's exit key */
    ERA_PRE_26FC5D3, /* the debug menu read Enter on its opening frame and on Alt+Enter */
    ERA_PRE_15C50EB, /* Q swapped weapons and quit while paused */
    ERA_TRUNK,       /* src/main.c at d4a857c, before the binding table */
    ERA_COUNT,
} LegacyEra;

RouteFn LegacyRouter(LegacyEra era);

#endif
