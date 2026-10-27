#include "game.h"

#include <math.h>
#include <string.h>

#include "raymath.h"

#define WALK_SPEED 4.5f
#define SPRINT_SPEED 6.5f
#define CROUCH_SPEED 2.2f
#define CROUCH_EASE_RATE 6.0f
#define JUMP_SPEED 5.0f
#define GRAVITY 15.0f
#define PITCH_LIMIT (89.0f * DEG2RAD)
#define SWAP_TIME 0.3f
#define HIT_POINTS 10
#define KILL_POINTS 60
#define HEADSHOT_KILL_POINTS 100
#define HITMARKER_TIME 0.18f
#define MUZZLE_FLASH_TIME 0.06f
#define STOP_DISTANCE 0.9f
#define SEPARATION_DISTANCE 0.85f
#define SEPARATION_SPEED 2.0f
#define ZOMBIE_TURN_TIME 0.12f
#define MIN_SPAWN_DISTANCE 7.0f
#define FIRST_SPAWN_DELAY 0.8f
#define SPRINTER_SPEED 4.0f
#define ATTACK_WINDUP 0.4f
#define ATTACK_COOLDOWN 1.0f
#define REGEN_RATE 50.0f
/* Below the 0.6m partition plus both 0.4m body radii, so a zombie cannot hit through the wall. */
#define ATTACK_RANGE 1.3f
#define DOOR_BOX {{-1.5f, 0.0f, -0.3f}, {1.5f, WALL_HEIGHT, 0.3f}}
#define DEFAULT_SEED 0x9E3779B9u
#define PLAYER_MOVER (-1)
#define VISIBILITY_EPSILON 0.01f
#define GROWL_INTERVAL 5.0f
#define GROWL_STAGGER 0.7f
#define GROWL_STAGGER_SLOTS 7

#define WEAPON_DEF_ROW(id, name_, damage_, fire_interval_, automatic_, mag_size_, reserve_max_, reload_time_, pellets_, \
                       spread_, wall_cost_, ...) \
    [WEAPON_##id] = { \
        .name = name_, .damage = damage_, .fire_interval = fire_interval_, .automatic = automatic_, \
        .mag_size = mag_size_, .reserve_max = reserve_max_, .reload_time = reload_time_, \
        .pellets = pellets_, .spread = spread_, .wall_cost = wall_cost_, \
    },
const WeaponDef WEAPONS[WEAPON_COUNT] = {WEAPON_LIST(WEAPON_DEF_ROW)};

#define PERK_DEF_ROW(id, name_, cost_, max_health_, reload_scale_, ...) \
    [PERK_##id] = {.name = name_, .cost = cost_, .max_health = max_health_, .reload_scale = reload_scale_},
const PerkDef PERKS[PERK_COUNT] = {PERK_LIST(PERK_DEF_ROW)};

#define DEBUG_WEAPON_ROW(id, name, ...) {"Give " name, DEBUG_WEAPON, WEAPON_##id},
#define DEBUG_PERK_ROW(id, name, ...) {"Give " name, DEBUG_PERK, PERK_##id},
const DebugCommand DEBUG_COMMANDS[] = {
    {"+5000 points", DEBUG_POINTS, 5000},
    WEAPON_LIST(DEBUG_WEAPON_ROW)
    PERK_LIST(DEBUG_PERK_ROW)
    {"Refill ammo", DEBUG_REFILL_AMMO, 0},
    {"Full health", DEBUG_HEAL, 0},
    {"God mode", DEBUG_GOD_MODE, 0},
    {"Open all doors", DEBUG_OPEN_DOORS, 0},
    {"Skip round", DEBUG_SKIP_ROUND, 0},
};
const int DEBUG_COMMAND_COUNT = (int)(sizeof DEBUG_COMMANDS / sizeof DEBUG_COMMANDS[0]);

const BoundingBox LEVEL_WALLS[] = {
    {{-10.6f, 0.0f, -16.6f}, {-10.0f, WALL_HEIGHT, 14.6f}},
    {{10.0f, 0.0f, -16.6f}, {10.6f, WALL_HEIGHT, 14.6f}},
    {{-10.0f, 0.0f, 14.0f}, {10.0f, WALL_HEIGHT, 14.6f}},
    {{-10.0f, 0.0f, -16.6f}, {10.0f, WALL_HEIGHT, -16.0f}},
    {{-10.0f, 0.0f, -0.3f}, {-1.5f, WALL_HEIGHT, 0.3f}},
    {{1.5f, 0.0f, -0.3f}, {10.0f, WALL_HEIGHT, 0.3f}},
};
const int LEVEL_WALL_COUNT = (int)(sizeof LEVEL_WALLS / sizeof LEVEL_WALLS[0]);

/* Not in LEVEL_WALLS: they block only a body tall enough to touch them, and zombies stoop under them. */
const BoundingBox LEVEL_OVERHANGS[] = {
    {{-10.0f, 1.3f, 2.8f}, {-6.5f, 1.6f, 4.8f}},
};
const int LEVEL_OVERHANG_COUNT = (int)(sizeof LEVEL_OVERHANGS / sizeof LEVEL_OVERHANGS[0]);

const Zone LEVEL_ZONES[ZONE_COUNT] = {
    {-10.0f, 10.0f, 0.0f, 14.0f},
    {-10.0f, 10.0f, -16.0f, 0.0f},
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
    {INTERACT_WALL_BUY, {{-10.0f, 0.9f, 5.6f}, {-9.88f, 2.3f, 8.4f}}, false, 2.6f, .weapon = WEAPON_SMG},
    {INTERACT_WALL_BUY, {{9.88f, 0.9f, 6.6f}, {10.0f, 2.3f, 9.4f}}, false, 2.6f, .weapon = WEAPON_SHOTGUN},
    {INTERACT_DOOR, DOOR_BOX, false, 3.0f, .door = 0},
    {INTERACT_PERK, {{8.4f, 0.0f, 12.7f}, {10.0f, 2.4f, 14.0f}}, true, 2.6f, .perk = PERK_VITALITY},
    {INTERACT_PERK, {{-10.0f, 0.0f, -1.6f}, {-8.4f, 2.4f, -0.3f}}, true, 2.6f, .perk = PERK_QUICK_HANDS},
};
const int LEVEL_INTERACTABLE_COUNT = (int)(sizeof LEVEL_INTERACTABLES / sizeof LEVEL_INTERACTABLES[0]);

_Static_assert(sizeof LEVEL_ZONES / sizeof LEVEL_ZONES[0] == ZONE_COUNT, "zone table matches ZONE_COUNT");
_Static_assert(ZONE_COUNT < 32, "the walkable mask key keeps one bit per zone and one bit that is always set");

static const Vector3 PLAYER_START = {0.0f, 0.0f, 10.0f};

static void InitPlayer(Player *p)
{
    memset(p, 0, sizeof *p);
    p->pos = PLAYER_START;
    p->eye_height = PLAYER_EYE_HEIGHT;
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
    return (Vector3){p->pos.x, p->pos.y + p->eye_height, p->pos.z};
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

int GameSolidCount(void)
{
    return LEVEL_WALL_COUNT + LEVEL_DOOR_COUNT + LEVEL_INTERACTABLE_COUNT;
}

/* Walls, closed doors and solid interactables share one index space so movement and bullets agree on what is solid. */
const BoundingBox *GameSolid(const GameState *g, int slot)
{
    if (slot < LEVEL_WALL_COUNT) return &LEVEL_WALLS[slot];
    slot -= LEVEL_WALL_COUNT;
    if (slot < LEVEL_DOOR_COUNT) {
        return g->zone_open[LEVEL_DOORS[slot].opens_zone] ? NULL : &LEVEL_DOORS[slot].box;
    }
    slot -= LEVEL_DOOR_COUNT;
    return LEVEL_INTERACTABLES[slot].solid ? &LEVEL_INTERACTABLES[slot].box : NULL;
}

static bool OverlapsSolid(const BoundingBox *b, Vector3 pos, float radius)
{
    return pos.x + radius > b->min.x && pos.x - radius < b->max.x && pos.z + radius > b->min.z && pos.z - radius < b->max.z;
}

bool GameBlocked(const GameState *g, Vector3 pos, float radius)
{
    for (int i = 0; i < GameSolidCount(); i++) {
        const BoundingBox *b = GameSolid(g, i);
        if (b != NULL && OverlapsSolid(b, pos, radius)) return true;
    }
    return false;
}

static bool ZombieTargetable(const Zombie *z)
{
    return z->state == ZOMBIE_CHASE || z->state == ZOMBIE_ATTACK;
}

/* Zombies stay on the floor, so a jump never changes where they can reach. */
static Vector3 PlayerFloorPoint(const GameState *g)
{
    return (Vector3){g->player.pos.x, 0.0f, g->player.pos.z};
}

/* A step is refused only when it closes on a body already within touching distance, so overlapping bodies can still part. */
static bool StepsIntoBody(Vector3 from, Vector3 to, Vector3 other)
{
    float before = hypotf(from.x - other.x, from.z - other.z);
    float after = hypotf(to.x - other.x, to.z - other.z);
    return after < PLAYER_RADIUS + ZOMBIE_RADIUS && after < before;
}

static bool PlayerBodyBlocked(Vector3 pos, float eye_height)
{
    float top = pos.y + eye_height + PLAYER_HEAD_CLEARANCE;
    for (int i = 0; i < LEVEL_OVERHANG_COUNT; i++) {
        const BoundingBox *b = &LEVEL_OVERHANGS[i];
        if (pos.x + PLAYER_RADIUS > b->min.x && pos.x - PLAYER_RADIUS < b->max.x &&
            pos.z + PLAYER_RADIUS > b->min.z && pos.z - PLAYER_RADIUS < b->max.z &&
            top > b->min.y && pos.y < b->max.y) {
            return true;
        }
    }
    return false;
}

/* mover is PLAYER_MOVER or the index of the zombie that moves. */
static bool MoveBlocked(const GameState *g, Vector3 from, Vector3 to, int mover)
{
    if (mover != PLAYER_MOVER) return GameBlocked(g, to, ZOMBIE_RADIUS) || StepsIntoBody(from, to, PlayerFloorPoint(g));
    if (GameBlocked(g, to, PLAYER_RADIUS) || PlayerBodyBlocked(to, g->player.eye_height)) return true;
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

static float NearestSolidDistance(const GameState *g, Ray ray)
{
    float nearest = INFINITY;
    for (int i = 0; i < GameSolidCount(); i++) {
        const BoundingBox *b = GameSolid(g, i);
        if (b == NULL) continue;
        RayCollision hit = GetRayCollisionBox(ray, *b);
        if (hit.hit && hit.distance < nearest) nearest = hit.distance;
    }
    for (int i = 0; i < LEVEL_OVERHANG_COUNT; i++) {
        RayCollision hit = GetRayCollisionBox(ray, LEVEL_OVERHANGS[i]);
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

static void PushEvent(GameState *g, GameEventKind kind, int arg)
{
    if (g->event_count >= GAME_EVENT_CAPACITY) return;
    g->events[g->event_count++] = (GameEvent){kind, arg};
}

static void RecordDamageNumber(GameState *g, const Zombie *z, Vector3 point, float amount, bool headshot, bool kill)
{
    int index = (int)(z - g->zombies);
    DamageNumber *slot = &g->damage_numbers[0];
    for (int i = 0; i < MAX_DAMAGE_NUMBERS; i++) {
        DamageNumber *n = &g->damage_numbers[i];
        if (n->zombie == index && n->timer == DAMAGE_NUMBER_TIME) {
            n->amount += amount;
            n->headshot = n->headshot || headshot;
            n->kill = n->kill || kill;
            return;
        }
        if (n->timer < slot->timer) slot = n;
    }
    *slot = (DamageNumber){point, amount, DAMAGE_NUMBER_TIME, index, headshot, kill};
}

static void DamageZombie(GameState *g, Zombie *z, Vector3 point, float damage, bool headshot)
{
    Player *p = &g->player;
    float amount = headshot ? damage * HEADSHOT_MULTIPLIER : damage;
    z->health -= amount;
    RecordDamageNumber(g, z, point, amount, headshot, z->health <= 0.0f);
    z->hit_flash = ZOMBIE_HIT_FLASH_TIME;
    p->hitmarker_timer = HITMARKER_TIME;
    if (z->health > 0.0f) {
        p->points += HIT_POINTS;
        p->hitmarker_kill = false;
        PushEvent(g, EVENT_HIT, 0);
        return;
    }
    PushEvent(g, headshot ? EVENT_HEADSHOT_KILL : EVENT_KILL, 0);
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
    if (hit != NULL) DamageZombie(g, hit, Vector3Add(ray.position, Vector3Scale(ray.direction, nearest)), damage, headshot);
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
    PushEvent(g, EVENT_FIRE, (int)slot->id);
    for (int i = 0; i < w->pellets; i++) {
        Ray ray = {PlayerEye(p), PelletDirection(g, w->spread)};
        CastShot(g, ray, w->damage);
    }
}

float ReloadDuration(const Player *p, WeaponId id)
{
    float scale = 1.0f;
    for (int i = 0; i < PERK_COUNT; i++) {
        if (PlayerHasPerk(p, (PerkId)i)) scale *= PERKS[i].reload_scale;
    }
    return WEAPONS[id].reload_time * scale;
}

static void StartReload(GameState *g)
{
    Player *p = &g->player;
    const WeaponSlot *slot = &p->slots[p->active];
    const WeaponDef *w = &WEAPONS[slot->id];
    if (p->reload_timer > 0.0f || slot->mag >= w->mag_size || slot->reserve <= 0) return;
    p->reload_timer = ReloadDuration(p, slot->id);
    PushEvent(g, EVENT_RELOAD, (int)slot->id);
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

    int target = p->active;
    if (in->select_slot >= 1 && in->select_slot <= p->slot_count) {
        target = in->select_slot - 1;
    } else if (in->cycle_steps != 0) {
        target = ((p->active + in->cycle_steps) % p->slot_count + p->slot_count) % p->slot_count;
    }
    if (target != p->active) {
        p->active = target;
        p->reload_timer = 0.0f;
        p->fire_cooldown = fmaxf(p->fire_cooldown, SWAP_TIME);
    }
    if (in->reload_pressed) StartReload(g);

    const WeaponSlot *slot = &p->slots[p->active];
    bool trigger = WEAPONS[slot->id].automatic ? in->fire_held : in->fire_pressed;
    if (!trigger || p->reload_timer > 0.0f || p->fire_cooldown > 0.0f) return;
    if (slot->mag > 0) {
        FireWeapon(g);
    } else {
        StartReload(g);
    }
}

static void UpdateMovement(GameState *g, const FrameInput *in, float dt)
{
    Player *p = &g->player;
    p->yaw += in->look.x;
    p->pitch = Clamp(p->pitch + in->look.y, -PITCH_LIMIT, PITCH_LIMIT);

    Vector3 forward = {sinf(p->yaw), 0.0f, -cosf(p->yaw)};
    Vector3 right = {cosf(p->yaw), 0.0f, sinf(p->yaw)};
    bool crouching = in->crouch_held || PlayerBodyBlocked(p->pos, PLAYER_EYE_HEIGHT);
    bool grounded = p->pos.y <= 0.0f && p->vertical_speed <= 0.0f;
    if (in->jump_pressed && grounded && !crouching) p->vertical_speed = JUMP_SPEED;
    p->vertical_speed -= GRAVITY * dt;
    float rise = p->pos.y + p->vertical_speed * dt;
    p->pos.y = fmaxf(rise, 0.0f);
    if (rise <= 0.0f) p->vertical_speed = 0.0f;

    float eye_target = crouching ? CROUCH_EYE_HEIGHT : PLAYER_EYE_HEIGHT;
    float eye_step = CROUCH_EASE_RATE * dt;
    p->eye_height += Clamp(eye_target - p->eye_height, -eye_step, eye_step);

    float speed = in->sprint_held && in->move.y > 0.0f ? SPRINT_SPEED : WALK_SPEED;
    if (crouching) speed = CROUCH_SPEED;
    Vector3 wish = Vector3Add(Vector3Scale(forward, in->move.y), Vector3Scale(right, in->move.x));
    p->pos = SlideMove(g, p->pos, Vector3Scale(wish, speed * dt), PLAYER_MOVER);
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
    return !need_distance || Vector3Distance(s->pos, PlayerFloorPoint(g)) >= MIN_SPAWN_DISTANCE;
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
        .growl_timer = GROWL_INTERVAL + GROWL_STAGGER * (float)((slot - g->zombies) % GROWL_STAGGER_SLOTS),
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
    if (g->phase == PHASE_GAME_OVER || g->god_mode) return;
    p->health -= amount;
    p->time_since_damage = 0.0f;
    p->damage_flash = DAMAGE_FLASH_TIME;
    PushEvent(g, EVENT_PLAYER_DAMAGE, 0);
    if (p->health > 0.0f) return;
    p->health = 0.0f;
    g->rounds_survived = g->round_number;
    EnterPhase(g, PHASE_GAME_OVER);
}

static void FillWalkable(const GameState *g, NavField *f)
{
    for (int c = 0; c < NAV_CELLS; c++) {
        f->walkable[c] = true;
        f->solid_free[c] = true;
    }
    for (int i = 0; i < GameSolidCount(); i++) {
        const BoundingBox *b = GameSolid(g, i);
        if (b == NULL) continue;
        float reach = fmaxf(ZOMBIE_RADIUS, NAV_CELL_SIZE / 2.0f);
        int first = NavCellAt((Vector3){b->min.x - reach, 0.0f, b->min.z - reach});
        int last = NavCellAt((Vector3){b->max.x + reach, 0.0f, b->max.z + reach});
        for (int row = first / NAV_COLS; row <= last / NAV_COLS; row++) {
            for (int c = row * NAV_COLS + first % NAV_COLS; c <= row * NAV_COLS + last % NAV_COLS; c++) {
                if (OverlapsSolid(b, NavCellCenter(c), ZOMBIE_RADIUS)) f->walkable[c] = false;
                if (OverlapsSolid(b, NavCellCenter(c), NAV_CELL_SIZE / 2.0f)) f->solid_free[c] = false;
            }
        }
    }
}

void GameUpdateNav(GameState *g)
{
    NavField *f = &g->nav;
    uint32_t open_zones = 1u << ZONE_COUNT;
    for (int i = 0; i < ZONE_COUNT; i++) open_zones |= g->zone_open[i] ? 1u << i : 0u;
    int goal = NavCellAt(g->player.pos);
    if (f->walkable_open_zones != open_zones) {
        FillWalkable(g, f);
        f->walkable_open_zones = open_zones;
    } else if (f->goal == goal) {
        return;
    }
    NavFlood(f, g->player.pos);
}

static Vector3 ZombieSteering(const GameState *g, const Zombie *z)
{
    if (z->state == ZOMBIE_ATTACK) return Vector3Zero();
    Vector3 target = PlayerFloorPoint(g);
    if (Vector3Distance(target, z->pos) <= STOP_DISTANCE) return Vector3Zero();
    return NavDirection(&g->nav, z->pos, target);
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
    if (Vector3LengthSqr(steer) > 0.0f) {
        float turn = remainderf(atan2f(steer.x, -steer.z) - z->yaw, 2.0f * PI);
        z->yaw = remainderf(z->yaw + turn * (1.0f - expf(-dt / ZOMBIE_TURN_TIME)), 2.0f * PI);
    }

    Vector3 to_player = Vector3Subtract(PlayerFloorPoint(g), z->pos);
    float distance = Vector3Length(to_player);
    z->attack_timer -= dt;
    z->growl_timer -= dt;
    if (z->growl_timer <= 0.0f) {
        PushEvent(g, EVENT_ZOMBIE_GROWL, index);
        z->growl_timer = GROWL_INTERVAL;
    }
    if (z->state == ZOMBIE_CHASE) {
        if (z->attack_timer > 0.0f || distance > ATTACK_RANGE) return;
        z->state = ZOMBIE_ATTACK;
        z->attack_timer = ATTACK_WINDUP;
        PushEvent(g, EVENT_ZOMBIE_ATTACK, index);
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
    PushEvent(g, EVENT_ROUND_START, g->round_number);
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
            PushEvent(g, EVENT_ROUND_END, g->round_number);
            g->round_number++;
            EnterPhase(g, PHASE_INTERMISSION);
        }
        break;
    case PHASE_GAME_OVER:
        break;
    }
}

bool PlayerHasPerk(const Player *p, PerkId id)
{
    return (p->perks & (1u << id)) != 0;
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
        WeaponId id = it->weapon;
        const WeaponSlot *owned = OwnedSlot(&g->player, id);
        info.active = owned == NULL || owned->mag < WEAPONS[id].mag_size || owned->reserve < WEAPONS[id].reserve_max;
        info.cost = WEAPONS[id].wall_cost;
        info.verb = owned != NULL ? "refill ammo for" : "buy";
        info.subject = WEAPONS[id].name;
        break;
    }
    case INTERACT_DOOR:
        info.active = !g->zone_open[LEVEL_DOORS[it->door].opens_zone];
        info.cost = LEVEL_DOORS[it->door].cost;
        info.verb = "open";
        info.subject = "the door";
        break;
    case INTERACT_PERK: {
        PerkId id = it->perk;
        info.active = !PlayerHasPerk(&g->player, id);
        info.cost = PERKS[id].cost;
        info.verb = "drink";
        info.subject = PERKS[id].name;
        break;
    }
    }
    return info;
}

static void GrantPerk(GameState *g, PerkId id)
{
    g->player.perks |= (uint8_t)(1u << id);
    if (PERKS[id].max_health > 0.0f) {
        g->player.max_health = PERKS[id].max_health;
        g->player.health = PERKS[id].max_health;
    }
}

static void ApplyInteraction(GameState *g, const Interactable *it)
{
    switch (it->kind) {
    case INTERACT_WALL_BUY:
        GiveWeapon(&g->player, it->weapon);
        break;
    case INTERACT_DOOR:
        g->zone_open[LEVEL_DOORS[it->door].opens_zone] = true;
        break;
    case INTERACT_PERK:
        GrantPerk(g, it->perk);
        break;
    }
}

static void SkipRound(GameState *g)
{
    if (g->phase == PHASE_INTERMISSION) {
        g->phase_timer = INTERMISSION_TIME;
        return;
    }
    g->zombies_left_to_spawn = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g->zombies[i];
        if (z->state != ZOMBIE_CHASE && z->state != ZOMBIE_ATTACK) continue;
        z->state = ZOMBIE_DYING;
        z->dying_timer = ZOMBIE_DYING_TIME;
    }
}

void GameDebugApply(GameState *g, const DebugCommand *cmd)
{
    Player *p = &g->player;
    if (g->phase == PHASE_GAME_OVER) return;
    switch (cmd->kind) {
    case DEBUG_POINTS:
        p->points += cmd->arg;
        break;
    case DEBUG_WEAPON:
        GiveWeapon(p, (WeaponId)cmd->arg);
        break;
    case DEBUG_PERK:
        GrantPerk(g, (PerkId)cmd->arg);
        break;
    case DEBUG_REFILL_AMMO:
        for (int i = 0; i < p->slot_count; i++) {
            p->slots[i].mag = WEAPONS[p->slots[i].id].mag_size;
            p->slots[i].reserve = WEAPONS[p->slots[i].id].reserve_max;
        }
        p->reload_timer = 0.0f;
        break;
    case DEBUG_HEAL:
        p->health = p->max_health;
        break;
    case DEBUG_GOD_MODE:
        g->god_mode = !g->god_mode;
        break;
    case DEBUG_OPEN_DOORS:
        for (int i = 0; i < ZONE_COUNT; i++) g->zone_open[i] = true;
        break;
    case DEBUG_SKIP_ROUND:
        SkipRound(g);
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
    if (g->player.points < cost) {
        PushEvent(g, EVENT_PURCHASE_DENIED, index);
        return;
    }
    PushEvent(g, EVENT_PURCHASE, index);
    g->player.points -= cost;
    ApplyInteraction(g, &LEVEL_INTERACTABLES[index]);
}

void GameUpdate(GameState *g, const FrameInput *in, float dt)
{
    g->event_count = 0;
    g->phase_timer += dt;
    for (int i = 0; i < MAX_DAMAGE_NUMBERS; i++) g->damage_numbers[i].timer = fmaxf(0.0f, g->damage_numbers[i].timer - dt);
    if (g->phase == PHASE_GAME_OVER) {
        for (int i = 0; i < MAX_ZOMBIES; i++) TickZombieTimers(&g->zombies[i], dt);
        if (in->restart_pressed && g->phase_timer >= RESTART_DELAY) GameInit(g, RngNext(g));
        return;
    }
    UpdateMovement(g, in, dt);
    UpdateInteraction(g, in);
    UpdateWeapon(g, in, dt);
    GameUpdateNav(g);
    for (int i = 0; i < MAX_ZOMBIES; i++) UpdateZombie(g, i, dt);
    UpdateHealth(g, dt);
    UpdateRound(g, dt);
}
