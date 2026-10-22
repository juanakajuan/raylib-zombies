#include "game.h"

#include <math.h>
#include <string.h>

#include "raymath.h"

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
#define STOP_DISTANCE 0.9f
#define SEPARATION_DISTANCE 0.85f
#define SEPARATION_SPEED 2.0f
#define MIN_SPAWN_DISTANCE 7.0f
#define FIRST_SPAWN_DELAY 0.8f
#define SPRINTER_SPEED 4.0f
#define ATTACK_WINDUP 0.4f
#define ATTACK_COOLDOWN 1.0f
#define REGEN_RATE 50.0f
#define PERK_NAME "Vitality Brew"
/* Below the 0.6m partition plus both 0.4m body radii, so a zombie cannot hit through the wall. */
#define ATTACK_RANGE 1.3f
#define DOOR_BOX {{-1.5f, 0.0f, -0.3f}, {1.5f, WALL_HEIGHT, 0.3f}}
#define DEFAULT_SEED 0x9E3779B9u
#define PLAYER_MOVER (-1)
#define VISIBILITY_EPSILON 0.01f

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
    {{8.5f, 0.0f, 11.0f}, 0},
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
    {INTERACT_PERK, {{8.4f, 0.0f, 12.7f}, {10.0f, 2.4f, 14.0f}}, true, 2.6f, 2500},
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

static bool ZombieTargetable(const Zombie *z)
{
    return z->state == ZOMBIE_CHASE || z->state == ZOMBIE_ATTACK;
}

/* A step is refused only when it closes on a body already within touching distance, so overlapping bodies can still part. */
static bool StepsIntoBody(Vector3 from, Vector3 to, Vector3 other)
{
    float before = hypotf(from.x - other.x, from.z - other.z);
    float after = hypotf(to.x - other.x, to.z - other.z);
    return after < PLAYER_RADIUS + ZOMBIE_RADIUS && after < before;
}

/* mover is PLAYER_MOVER or the index of the zombie that moves. */
static bool MoveBlocked(const GameState *g, Vector3 from, Vector3 to, int mover)
{
    if (mover != PLAYER_MOVER) return GameBlocked(g, to, ZOMBIE_RADIUS) || StepsIntoBody(from, to, g->player.pos);
    if (GameBlocked(g, to, PLAYER_RADIUS)) return true;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (ZombieTargetable(&g->zombies[i]) && StepsIntoBody(from, to, g->zombies[i].pos)) return true;
    }
    return false;
}

static Vector3 SlideMove(const GameState *g, Vector3 pos, Vector3 delta, int mover)
{
    Vector3 trial = {pos.x + delta.x, pos.y, pos.z};
    if (MoveBlocked(g, pos, trial, mover)) trial.x = pos.x;
    Vector3 from = trial;
    trial.z += delta.z;
    if (MoveBlocked(g, from, trial, mover)) trial.z = from.z;
    return trial;
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

/* Visible means the ray to the box centre reaches the box before any other solid. The epsilon covers a panel whose face is flush with its wall. */
bool GameInteractableVisible(const GameState *g, int index, Vector3 eye)
{
    BoundingBox box = LEVEL_INTERACTABLES[index].box;
    Ray ray = {eye, Vector3Normalize(Vector3Subtract(BoxCenter(box), eye))};
    RayCollision self = GetRayCollisionBox(ray, box);
    return self.hit && NearestSolidDistance(g, ray) >= self.distance - VISIBILITY_EPSILON;
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
        p->fire_cooldown = fmaxf(p->fire_cooldown, SWAP_TIME);
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
    p->pos = SlideMove(g, p->pos, Vector3Scale(wish, speed * dt), PLAYER_MOVER);
    g->player_zone = ZoneAt(p->pos, g->player_zone);
}

int GameAliveZombies(const GameState *g)
{
    int alive = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (ZombieTargetable(&g->zombies[i])) alive++;
    }
    return alive;
}

static int RoundZombieCount(int round)
{
    return 6 + 3 * (round - 1);
}

static float RoundZombieHealth(int round)
{
    float health = 150.0f;
    for (int r = 2; r <= round; r++) health = r <= 9 ? health + 100.0f : health * 1.1f;
    return health;
}

static float RoundZombieSpeed(GameState *g)
{
    int round = g->round_number;
    float speed = fminf(1.2f + 0.12f * (float)(round - 1), 3.0f);
    float sprinter_chance = fminf(0.6f, 0.15f * (float)(round - 3));
    if (round > 3 && RngFloat(g) < sprinter_chance) speed = SPRINTER_SPEED;
    return speed * (0.9f + 0.2f * RngFloat(g));
}

static float SpawnInterval(int round)
{
    return fmaxf(0.4f, 1.6f - 0.1f * (float)(round - 1));
}

static bool SpawnEligible(const GameState *g, int index, bool need_distance)
{
    const SpawnPoint *s = &LEVEL_SPAWNS[index];
    if (!g->zone_open[s->zone]) return false;
    return !need_distance || Vector3Distance(s->pos, g->player.pos) >= MIN_SPAWN_DISTANCE;
}

static int PickSpawn(GameState *g)
{
    for (int pass = 0; pass < 2; pass++) {
        bool need_distance = pass == 0;
        int eligible = 0;
        for (int i = 0; i < LEVEL_SPAWN_COUNT; i++) eligible += SpawnEligible(g, i, need_distance);
        if (eligible == 0) continue;
        int pick = (int)(RngNext(g) % (uint32_t)eligible);
        for (int i = 0; i < LEVEL_SPAWN_COUNT; i++) {
            if (SpawnEligible(g, i, need_distance) && pick-- == 0) return i;
        }
    }
    return -1;
}

static bool SpawnZombie(GameState *g)
{
    Zombie *slot = NULL;
    for (int i = 0; i < MAX_ZOMBIES && slot == NULL; i++) {
        if (g->zombies[i].state == ZOMBIE_FREE) slot = &g->zombies[i];
    }
    int spawn = PickSpawn(g);
    if (slot == NULL || spawn < 0) return false;

    float health = RoundZombieHealth(g->round_number);
    *slot = (Zombie){
        .state = ZOMBIE_CHASE,
        .pos = LEVEL_SPAWNS[spawn].pos,
        .health = health,
        .max_health = health,
        .speed = RoundZombieSpeed(g),
        .attack_timer = 0.6f,
        .zone = LEVEL_SPAWNS[spawn].zone,
    };
    return true;
}

static void EnterPhase(GameState *g, GamePhase phase)
{
    g->phase = phase;
    g->phase_timer = 0.0f;
}

static void DamagePlayer(GameState *g, float amount)
{
    Player *p = &g->player;
    if (g->phase == PHASE_GAME_OVER) return;
    p->health -= amount;
    p->time_since_damage = 0.0f;
    p->damage_flash = DAMAGE_FLASH_TIME;
    if (p->health > 0.0f) return;
    p->health = 0.0f;
    g->rounds_survived = g->round_number;
    EnterPhase(g, PHASE_GAME_OVER);
}

static Vector3 ZombieSteering(const GameState *g, const Zombie *z)
{
    if (z->state == ZOMBIE_ATTACK) return Vector3Zero();
    Vector3 target = g->player.pos;
    if (z->zone != g->player_zone) {
        target = (Vector3){LEVEL_ZONES[z->zone].portal.x, 0.0f, LEVEL_ZONES[z->zone].portal.y};
    } else if (Vector3Distance(target, z->pos) <= STOP_DISTANCE) {
        return Vector3Zero();
    }
    return Vector3Normalize(Vector3Subtract(target, z->pos));
}

static Vector3 ZombieSeparation(const GameState *g, int index)
{
    const Zombie *z = &g->zombies[index];
    Vector3 push = Vector3Zero();
    for (int j = 0; j < MAX_ZOMBIES; j++) {
        const Zombie *other = &g->zombies[j];
        if (j == index || !ZombieTargetable(other)) continue;
        Vector3 away = Vector3Subtract(z->pos, other->pos);
        float distance = Vector3Length(away);
        if (distance >= SEPARATION_DISTANCE) continue;
        /* Zombies stacked exactly on each other need a deterministic direction to part ways. */
        if (distance < 1e-4f) {
            away = (Vector3){index < j ? 1.0f : -1.0f, 0.0f, 0.0f};
            distance = 1.0f;
        }
        float overlap = (SEPARATION_DISTANCE - distance) / SEPARATION_DISTANCE;
        push = Vector3Add(push, Vector3Scale(away, overlap / distance));
    }
    return push;
}

static void TickZombieTimers(Zombie *z, float dt)
{
    z->hit_flash = fmaxf(0.0f, z->hit_flash - dt);
    if (z->state != ZOMBIE_DYING) return;
    z->dying_timer -= dt;
    if (z->dying_timer <= 0.0f) z->state = ZOMBIE_FREE;
}

static void UpdateZombie(GameState *g, int index, float dt)
{
    Zombie *z = &g->zombies[index];
    TickZombieTimers(z, dt);
    if (!ZombieTargetable(z)) return;

    Vector3 steer = ZombieSteering(g, z);
    Vector3 velocity = Vector3Add(Vector3Scale(steer, z->speed), Vector3Scale(ZombieSeparation(g, index), SEPARATION_SPEED));
    z->pos = SlideMove(g, z->pos, Vector3Scale(velocity, dt), index);
    z->zone = ZoneAt(z->pos, z->zone);
    if (Vector3LengthSqr(steer) > 0.0f) z->yaw = atan2f(steer.x, -steer.z);

    Vector3 to_player = Vector3Subtract(g->player.pos, z->pos);
    float distance = Vector3Length(to_player);
    z->attack_timer -= dt;
    if (z->state == ZOMBIE_CHASE) {
        if (z->attack_timer > 0.0f || distance > ATTACK_RANGE) return;
        z->state = ZOMBIE_ATTACK;
        z->attack_timer = ATTACK_WINDUP;
        z->yaw = atan2f(to_player.x, -to_player.z);
    } else if (z->attack_timer <= 0.0f) {
        if (distance <= ATTACK_RANGE) DamagePlayer(g, ZOMBIE_DAMAGE);
        z->state = ZOMBIE_CHASE;
        z->attack_timer = ATTACK_COOLDOWN;
    }
}

static void UpdateHealth(GameState *g, float dt)
{
    Player *p = &g->player;
    p->time_since_damage += dt;
    p->damage_flash = fmaxf(0.0f, p->damage_flash - dt);
    if (p->time_since_damage >= REGEN_DELAY) p->health = fminf(p->max_health, p->health + REGEN_RATE * dt);
}

static void StartRound(GameState *g)
{
    g->zombies_left_to_spawn = RoundZombieCount(g->round_number);
    g->spawn_timer = FIRST_SPAWN_DELAY;
    EnterPhase(g, PHASE_ROUND_ACTIVE);
}

static void UpdateSpawner(GameState *g, float dt)
{
    g->spawn_timer -= dt;
    if (g->zombies_left_to_spawn == 0 || g->spawn_timer > 0.0f || GameAliveZombies(g) >= MAX_ALIVE_ZOMBIES) return;
    if (SpawnZombie(g)) {
        g->zombies_left_to_spawn--;
        g->spawn_timer = SpawnInterval(g->round_number);
    }
}

static void UpdateRound(GameState *g, float dt)
{
    switch (g->phase) {
    case PHASE_INTERMISSION:
        if (g->phase_timer >= INTERMISSION_TIME) StartRound(g);
        break;
    case PHASE_ROUND_ACTIVE:
        UpdateSpawner(g, dt);
        if (g->zombies_left_to_spawn == 0 && GameAliveZombies(g) == 0) {
            g->round_number++;
            EnterPhase(g, PHASE_INTERMISSION);
        }
        break;
    case PHASE_GAME_OVER:
        break;
    }
}

bool PlayerHasPerk(const Player *p)
{
    return p->max_health > PLAYER_BASE_HEALTH;
}

static const WeaponSlot *OwnedSlot(const Player *p, WeaponId id)
{
    for (int i = 0; i < p->slot_count; i++) {
        if (p->slots[i].id == id) return &p->slots[i];
    }
    return NULL;
}

static void GiveWeapon(Player *p, WeaponId id)
{
    int slot = p->active;
    for (int i = 0; i < p->slot_count; i++) {
        if (p->slots[i].id == id) slot = i;
    }
    if (OwnedSlot(p, id) == NULL && p->slot_count < MAX_SLOTS) slot = p->slot_count++;
    p->slots[slot] = (WeaponSlot){id, WEAPONS[id].mag_size, WEAPONS[id].reserve_max};
    p->active = slot;
    p->reload_timer = 0.0f;
}

InteractionInfo GameDescribeInteractable(const GameState *g, int index)
{
    const Interactable *it = &LEVEL_INTERACTABLES[index];
    InteractionInfo info = {0};
    switch (it->kind) {
    case INTERACT_WALL_BUY: {
        WeaponId id = (WeaponId)it->payload;
        const WeaponSlot *owned = OwnedSlot(&g->player, id);
        info.active = owned == NULL || owned->mag < WEAPONS[id].mag_size || owned->reserve < WEAPONS[id].reserve_max;
        info.cost = WEAPONS[id].wall_cost;
        info.verb = owned != NULL ? "refill ammo for" : "buy";
        info.subject = WEAPONS[id].name;
        break;
    }
    case INTERACT_DOOR:
        info.active = !g->zone_open[LEVEL_DOORS[it->payload].opens_zone];
        info.cost = LEVEL_DOORS[it->payload].cost;
        info.verb = "open";
        info.subject = "the door";
        break;
    case INTERACT_PERK:
        info.active = !PlayerHasPerk(&g->player);
        info.cost = it->payload;
        info.verb = "drink";
        info.subject = PERK_NAME;
        break;
    }
    return info;
}

static void ApplyInteraction(GameState *g, const Interactable *it)
{
    switch (it->kind) {
    case INTERACT_WALL_BUY:
        GiveWeapon(&g->player, (WeaponId)it->payload);
        break;
    case INTERACT_DOOR:
        g->zone_open[LEVEL_DOORS[it->payload].opens_zone] = true;
        break;
    case INTERACT_PERK:
        g->player.max_health = PERK_MAX_HEALTH;
        g->player.health = PERK_MAX_HEALTH;
        break;
    }
}

int GameNearestInteractable(const GameState *g)
{
    int nearest = -1;
    float best = INFINITY;
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
        if (!GameDescribeInteractable(g, i).active) continue;
        Vector3 center = BoxCenter(LEVEL_INTERACTABLES[i].box);
        float distance = hypotf(center.x - g->player.pos.x, center.z - g->player.pos.z);
        if (distance <= LEVEL_INTERACTABLES[i].radius && distance < best) {
            best = distance;
            nearest = i;
        }
    }
    return nearest;
}

static void UpdateInteraction(GameState *g, const FrameInput *in)
{
    if (!in->interact_pressed) return;
    int index = GameNearestInteractable(g);
    if (index < 0) return;
    int cost = GameDescribeInteractable(g, index).cost;
    if (g->player.points < cost) return;
    g->player.points -= cost;
    ApplyInteraction(g, &LEVEL_INTERACTABLES[index]);
}

void GameUpdate(GameState *g, const FrameInput *in, float dt)
{
    g->phase_timer += dt;
    if (g->phase == PHASE_GAME_OVER) {
        for (int i = 0; i < MAX_ZOMBIES; i++) TickZombieTimers(&g->zombies[i], dt);
        if (in->restart_pressed && g->phase_timer >= RESTART_DELAY) GameInit(g, RngNext(g));
        return;
    }
    UpdateMovement(g, in, dt);
    UpdateInteraction(g, in);
    UpdateWeapon(g, in, dt);
    for (int i = 0; i < MAX_ZOMBIES; i++) UpdateZombie(g, i, dt);
    UpdateHealth(g, dt);
    UpdateRound(g, dt);
}
