#include "game.h"

#include <math.h>
#include <string.h>

#include "raymath.h"

#define PARTITION_THICKNESS 0.6f
#define WALK_SPEED 4.5f
#define SPRINT_SPEED 6.5f
#define PITCH_LIMIT (89.0f * DEG2RAD)
#define SWAP_TIME 0.3f
#define HEADSHOT_MULTIPLIER 2.0f
#define HIT_POINTS 10
#define KILL_POINTS 60
#define HEADSHOT_KILL_POINTS 100
#define HITMARKER_TIME 0.18f
#define MUZZLE_FLASH_TIME 0.06f
#define ZOMBIE_HIT_FLASH_TIME 0.12f
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

static uint32_t RngNext(GameState *g)
{
    uint32_t x = g->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng_state = x;
    return x;
}

static float RngFloat(GameState *g)
{
    return (float)(RngNext(g) >> 8) / 16777216.0f;
}

Vector3 BoxCenter(BoundingBox box)
{
    return Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
}

Vector3 PlayerEye(const Player *p)
{
    return (Vector3){p->pos.x, PLAYER_EYE_HEIGHT, p->pos.z};
}

Vector3 PlayerForward(const Player *p)
{
    return (Vector3){cosf(p->pitch) * sinf(p->yaw), sinf(p->pitch), -cosf(p->pitch) * cosf(p->yaw)};
}

BoundingBox ZombieBodyBox(const Zombie *z)
{
    return (BoundingBox){{z->pos.x - 0.32f, 0.0f, z->pos.z - 0.32f}, {z->pos.x + 0.32f, 1.45f, z->pos.z + 0.32f}};
}

BoundingBox ZombieHeadBox(const Zombie *z)
{
    return (BoundingBox){{z->pos.x - 0.2f, 1.45f, z->pos.z - 0.2f}, {z->pos.x + 0.2f, 1.85f, z->pos.z + 0.2f}};
}

static int SolidSlotCount(void)
{
    return LEVEL_WALL_COUNT + LEVEL_DOOR_COUNT + LEVEL_INTERACTABLE_COUNT;
}

/* Walls, closed doors and solid interactables share one index space so movement and bullets agree on what is solid. */
static const BoundingBox *SolidSlot(const GameState *g, int slot)
{
    if (slot < LEVEL_WALL_COUNT) return &LEVEL_WALLS[slot];
    slot -= LEVEL_WALL_COUNT;
    if (slot < LEVEL_DOOR_COUNT) {
        return g->zone_open[LEVEL_DOORS[slot].opens_zone] ? NULL : &LEVEL_DOORS[slot].box;
    }
    slot -= LEVEL_DOOR_COUNT;
    return LEVEL_INTERACTABLES[slot].solid ? &LEVEL_INTERACTABLES[slot].box : NULL;
}

bool GameBlocked(const GameState *g, Vector3 pos, float radius)
{
    for (int i = 0; i < SolidSlotCount(); i++) {
        const BoundingBox *b = SolidSlot(g, i);
        if (b == NULL) continue;
        if (pos.x + radius > b->min.x && pos.x - radius < b->max.x &&
            pos.z + radius > b->min.z && pos.z - radius < b->max.z) {
            return true;
        }
    }
    return false;
}

static Vector3 SlideMove(const GameState *g, Vector3 pos, Vector3 delta, float radius)
{
    Vector3 next = pos;
    next.x += delta.x;
    if (GameBlocked(g, next, radius)) next.x = pos.x;
    next.z += delta.z;
    if (GameBlocked(g, next, radius)) next.z = pos.z;
    return next;
}

static int ZoneAt(Vector3 pos, int fallback)
{
    for (int i = 0; i < ZONE_COUNT; i++) {
        const Zone *z = &LEVEL_ZONES[i];
        if (pos.x >= z->min_x && pos.x <= z->max_x && pos.z >= z->min_z && pos.z <= z->max_z) return i;
    }
    return fallback;
}

static float NearestSolidDistance(const GameState *g, Ray ray)
{
    float nearest = INFINITY;
    for (int i = 0; i < SolidSlotCount(); i++) {
        const BoundingBox *b = SolidSlot(g, i);
        if (b == NULL) continue;
        RayCollision hit = GetRayCollisionBox(ray, *b);
        if (hit.hit && hit.distance < nearest) nearest = hit.distance;
    }
    return nearest;
}

static bool ZombieTargetable(const Zombie *z)
{
    return z->state == ZOMBIE_CHASE || z->state == ZOMBIE_ATTACK;
}

static void DamageZombie(GameState *g, Zombie *z, float damage, bool headshot)
{
    Player *p = &g->player;
    z->health -= headshot ? damage * HEADSHOT_MULTIPLIER : damage;
    z->hit_flash = ZOMBIE_HIT_FLASH_TIME;
    p->hitmarker_timer = HITMARKER_TIME;
    if (z->health > 0.0f) {
        p->points += HIT_POINTS;
        p->hitmarker_kill = false;
        return;
    }
    p->points += headshot ? HEADSHOT_KILL_POINTS : KILL_POINTS;
    p->hitmarker_kill = true;
    g->kills++;
    z->state = ZOMBIE_DYING;
    z->dying_timer = ZOMBIE_DYING_TIME;
}

static void CastShot(GameState *g, Ray ray, float damage)
{
    float nearest = NearestSolidDistance(g, ray);
    Zombie *hit = NULL;
    bool headshot = false;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g->zombies[i];
        if (!ZombieTargetable(z)) continue;
        RayCollision head = GetRayCollisionBox(ray, ZombieHeadBox(z));
        if (head.hit && head.distance < nearest) {
            nearest = head.distance;
            hit = z;
            headshot = true;
        }
        RayCollision body = GetRayCollisionBox(ray, ZombieBodyBox(z));
        if (body.hit && body.distance < nearest) {
            nearest = body.distance;
            hit = z;
            headshot = false;
        }
    }
    if (hit != NULL) DamageZombie(g, hit, damage, headshot);
}

static Vector3 PelletDirection(GameState *g, float spread)
{
    const Player *p = &g->player;
    Vector3 forward = PlayerForward(p);
    if (spread <= 0.0f) return forward;
    Vector3 right = {cosf(p->yaw), 0.0f, sinf(p->yaw)};
    Vector3 up = Vector3CrossProduct(right, forward);
    float dx = (RngFloat(g) * 2.0f - 1.0f) * spread;
    float dy = (RngFloat(g) * 2.0f - 1.0f) * spread;
    return Vector3Normalize(Vector3Add(forward, Vector3Add(Vector3Scale(right, dx), Vector3Scale(up, dy))));
}

static void FireWeapon(GameState *g)
{
    Player *p = &g->player;
    WeaponSlot *slot = &p->slots[p->active];
    const WeaponDef *w = &WEAPONS[slot->id];
    slot->mag--;
    p->fire_cooldown = w->fire_interval;
    p->muzzle_timer = MUZZLE_FLASH_TIME;
    for (int i = 0; i < w->pellets; i++) {
        Ray ray = {PlayerEye(p), PelletDirection(g, w->spread)};
        CastShot(g, ray, w->damage);
    }
}

static void StartReload(Player *p)
{
    const WeaponSlot *slot = &p->slots[p->active];
    const WeaponDef *w = &WEAPONS[slot->id];
    if (p->reload_timer > 0.0f || slot->mag >= w->mag_size || slot->reserve <= 0) return;
    p->reload_timer = w->reload_time;
}

static void FinishReload(Player *p)
{
    WeaponSlot *slot = &p->slots[p->active];
    int missing = WEAPONS[slot->id].mag_size - slot->mag;
    int moved = missing < slot->reserve ? missing : slot->reserve;
    slot->mag += moved;
    slot->reserve -= moved;
}

static void UpdateWeapon(GameState *g, const FrameInput *in, float dt)
{
    Player *p = &g->player;
    p->fire_cooldown = fmaxf(0.0f, p->fire_cooldown - dt);
    p->hitmarker_timer = fmaxf(0.0f, p->hitmarker_timer - dt);
    p->muzzle_timer = fmaxf(0.0f, p->muzzle_timer - dt);

    if (p->reload_timer > 0.0f) {
        p->reload_timer -= dt;
        if (p->reload_timer <= 0.0f) {
            p->reload_timer = 0.0f;
            FinishReload(p);
        }
    }

    if (in->swap_pressed && p->slot_count > 1) {
        p->active = (p->active + 1) % p->slot_count;
        p->reload_timer = 0.0f;
        p->fire_cooldown = SWAP_TIME;
    }
    if (in->reload_pressed) StartReload(p);

    const WeaponSlot *slot = &p->slots[p->active];
    bool trigger = WEAPONS[slot->id].automatic ? in->fire_held : in->fire_pressed;
    if (!trigger || p->reload_timer > 0.0f || p->fire_cooldown > 0.0f) return;
    if (slot->mag > 0) {
        FireWeapon(g);
    } else {
        StartReload(p);
    }
}

static void UpdateMovement(GameState *g, const FrameInput *in, float dt)
{
    Player *p = &g->player;
    p->yaw += in->look.x;
    p->pitch = Clamp(p->pitch + in->look.y, -PITCH_LIMIT, PITCH_LIMIT);

    Vector3 forward = {sinf(p->yaw), 0.0f, -cosf(p->yaw)};
    Vector3 right = {cosf(p->yaw), 0.0f, sinf(p->yaw)};
    float speed = in->sprint_held && in->move.y > 0.0f ? SPRINT_SPEED : WALK_SPEED;
    Vector3 wish = Vector3Add(Vector3Scale(forward, in->move.y), Vector3Scale(right, in->move.x));
    p->pos = SlideMove(g, p->pos, Vector3Scale(wish, speed * dt), PLAYER_RADIUS);
    g->player_zone = ZoneAt(p->pos, g->player_zone);
}

void GameUpdate(GameState *g, const FrameInput *in, float dt)
{
    g->phase_timer += dt;
    UpdateMovement(g, in, dt);
    UpdateWeapon(g, in, dt);
}
