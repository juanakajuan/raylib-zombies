#include "game.h"

#include <string.h>

#define PARTITION_THICKNESS 0.6f
#define DOOR_BOX {{-1.5f, 0.0f, -0.3f}, {1.5f, WALL_HEIGHT, 0.3f}}
#define DEFAULT_SEED 0x9E3779B9u

const WeaponDef WEAPONS[WEAPON_COUNT] = {
    [WEAPON_PISTOL] = {
        .name = "M1911", .damage = 40.0f, .fire_interval = 0.28f, .automatic = false,
        .mag_size = 8, .reserve_max = 96, .reload_time = 1.4f,
        .pellets = 1, .spread = 0.0f, .wall_cost = 0,
    },
    [WEAPON_SMG] = {
        .name = "MP5K", .damage = 30.0f, .fire_interval = 0.085f, .automatic = true,
        .mag_size = 32, .reserve_max = 192, .reload_time = 1.8f,
        .pellets = 1, .spread = 0.012f, .wall_cost = 1000,
    },
    [WEAPON_SHOTGUN] = {
        .name = "Shotgun", .damage = 26.0f, .fire_interval = 0.8f, .automatic = false,
        .mag_size = 6, .reserve_max = 54, .reload_time = 2.4f,
        .pellets = 6, .spread = 0.055f, .wall_cost = 1500,
    },
};

const BoundingBox LEVEL_WALLS[] = {
    {{-10.6f, 0.0f, -16.6f}, {-10.0f, WALL_HEIGHT, 14.6f}},
    {{10.0f, 0.0f, -16.6f}, {10.6f, WALL_HEIGHT, 14.6f}},
    {{-10.0f, 0.0f, 14.0f}, {10.0f, WALL_HEIGHT, 14.6f}},
    {{-10.0f, 0.0f, -16.6f}, {10.0f, WALL_HEIGHT, -16.0f}},
    {{-10.0f, 0.0f, -0.3f}, {-1.5f, WALL_HEIGHT, 0.3f}},
    {{1.5f, 0.0f, -0.3f}, {10.0f, WALL_HEIGHT, 0.3f}},
};
const int LEVEL_WALL_COUNT = (int)(sizeof LEVEL_WALLS / sizeof LEVEL_WALLS[0]);

const Zone LEVEL_ZONES[ZONE_COUNT] = {
    {-10.0f, 10.0f, 0.0f, 14.0f, {0.0f, 0.0f}},
    {-10.0f, 10.0f, -16.0f, 0.0f, {0.0f, 0.0f}},
};

const Door LEVEL_DOORS[] = {
    {.box = DOOR_BOX, .cost = 750, .opens_zone = 1},
};
const int LEVEL_DOOR_COUNT = (int)(sizeof LEVEL_DOORS / sizeof LEVEL_DOORS[0]);

const SpawnPoint LEVEL_SPAWNS[] = {
    {{-8.5f, 0.0f, 2.0f}, 0},
    {{8.5f, 0.0f, 2.0f}, 0},
    {{-8.5f, 0.0f, 12.5f}, 0},
    {{8.5f, 0.0f, 12.5f}, 0},
    {{-8.5f, 0.0f, -14.5f}, 1},
    {{0.0f, 0.0f, -14.5f}, 1},
    {{8.5f, 0.0f, -14.5f}, 1},
    {{-8.5f, 0.0f, -8.0f}, 1},
    {{8.5f, 0.0f, -8.0f}, 1},
};
const int LEVEL_SPAWN_COUNT = (int)(sizeof LEVEL_SPAWNS / sizeof LEVEL_SPAWNS[0]);

const Interactable LEVEL_INTERACTABLES[] = {
    {INTERACT_WALL_BUY, {{-10.0f, 0.9f, 5.6f}, {-9.88f, 2.3f, 8.4f}}, false, 2.6f, WEAPON_SMG},
    {INTERACT_WALL_BUY, {{9.88f, 0.9f, 6.6f}, {10.0f, 2.3f, 9.4f}}, false, 2.6f, WEAPON_SHOTGUN},
    {INTERACT_DOOR, DOOR_BOX, false, 3.0f, 0},
    {INTERACT_PERK, {{5.4f, 0.0f, 12.7f}, {7.0f, 2.4f, 14.0f}}, true, 2.6f, 2500},
};
const int LEVEL_INTERACTABLE_COUNT = (int)(sizeof LEVEL_INTERACTABLES / sizeof LEVEL_INTERACTABLES[0]);

_Static_assert(sizeof LEVEL_ZONES / sizeof LEVEL_ZONES[0] == ZONE_COUNT, "zone table matches ZONE_COUNT");

static const Vector3 PLAYER_START = {0.0f, 0.0f, 10.0f};

static void InitPlayer(Player *p)
{
    memset(p, 0, sizeof *p);
    p->pos = PLAYER_START;
    p->health = PLAYER_BASE_HEALTH;
    p->max_health = PLAYER_BASE_HEALTH;
    p->time_since_damage = REGEN_DELAY;
    p->points = START_POINTS;
    p->slots[0] = (WeaponSlot){WEAPON_PISTOL, WEAPONS[WEAPON_PISTOL].mag_size, WEAPONS[WEAPON_PISTOL].reserve_max};
    p->slot_count = 1;
}

void GameInit(GameState *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rng_state = seed != 0 ? seed : DEFAULT_SEED;
    g->phase = PHASE_INTERMISSION;
    g->round_number = 1;
    g->zone_open[0] = true;
    InitPlayer(&g->player);
}
