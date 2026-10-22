#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define DT (1.0f / 60.0f)

static int g_checks = 0;
static int g_failures = 0;

static void Fail(const char *file, int line, const char *expr, const char *fmt, ...)
{
    va_list args;
    g_failures++;
    fprintf(stderr, "FAIL %s:%d: %s\n    ", file, line, expr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

#define CHECK(cond, ...) \
    do { \
        g_checks++; \
        if (!(cond)) Fail(__FILE__, __LINE__, #cond, __VA_ARGS__); \
    } while (0)

static void AimAt(Player *p, Vector3 target)
{
    Vector3 eye = PlayerEye(p);
    float dx = target.x - eye.x;
    float dy = target.y - eye.y;
    float dz = target.z - eye.z;
    p->yaw = atan2f(dx, -dz);
    p->pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
}

static Zombie *PlaceZombie(GameState *g, Vector3 pos, float health)
{
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (g->zombies[i].state != ZOMBIE_FREE) continue;
        g->zombies[i] = (Zombie){
            .state = ZOMBIE_CHASE, .pos = pos, .health = health, .max_health = health,
            .speed = 1.2f, .attack_timer = 1.0f, .zone = pos.z >= 0.0f ? 0 : 1,
        };
        return &g->zombies[i];
    }
    return NULL;
}

static GameState NewActiveGame(void)
{
    GameState g;
    GameInit(&g, 99);
    g.phase = PHASE_ROUND_ACTIVE;
    g.zombies_left_to_spawn = 0;
    return g;
}

static void TestInit(void)
{
    GameState g;
    GameInit(&g, 1234);

    CHECK(g.round_number == 1, "round_number=%d", g.round_number);
    CHECK(g.phase == PHASE_INTERMISSION, "phase=%d", (int)g.phase);
    CHECK(g.player.points == START_POINTS, "points=%d", g.player.points);
    CHECK(g.player.slot_count == 1, "slot_count=%d", g.player.slot_count);
    CHECK(g.player.slots[0].id == WEAPON_PISTOL, "slot0=%d", (int)g.player.slots[0].id);
    CHECK(g.player.slots[0].mag == WEAPONS[WEAPON_PISTOL].mag_size, "mag=%d", g.player.slots[0].mag);
    CHECK(g.player.slots[0].reserve == WEAPONS[WEAPON_PISTOL].reserve_max, "reserve=%d", g.player.slots[0].reserve);
    CHECK(g.zone_open[0] && !g.zone_open[1], "zone_open=%d,%d", g.zone_open[0], g.zone_open[1]);
    CHECK(g.player.health == PLAYER_BASE_HEALTH, "health=%.1f", (double)g.player.health);
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        CHECK(g.zombies[i].state == ZOMBIE_FREE, "zombie %d state=%d", i, (int)g.zombies[i].state);
    }
}

static void TestShootingAndReload(void)
{
    GameState g = NewActiveGame();
    Zombie *z = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 4.0f}, 150.0f);
    Vector3 body = {z->pos.x, 0.7f, z->pos.z};
    int start_points = g.player.points;
    int start_mag = g.player.slots[0].mag;
    FrameInput fire = {.fire_pressed = true};

    AimAt(&g.player, body);
    GameUpdate(&g, &fire, DT);
    CHECK(z->health == 110.0f, "one pistol body hit leaves health=%.1f", (double)z->health);
    CHECK(g.player.points == start_points + 10, "points=%d expected %d", g.player.points, start_points + 10);
    CHECK(g.player.slots[0].mag == start_mag - 1, "mag=%d expected %d", g.player.slots[0].mag, start_mag - 1);
    CHECK(g.player.hitmarker_timer > 0.0f, "hitmarker_timer=%.3f", (double)g.player.hitmarker_timer);

    AimAt(&g.player, body);
    GameUpdate(&g, &fire, DT);
    CHECK(z->health == 110.0f, "fire cooldown must block the second shot, health=%.1f", (double)z->health);

    for (int i = 0; i < 180 && z->state == ZOMBIE_CHASE; i++) {
        AimAt(&g.player, (Vector3){z->pos.x, 0.7f, z->pos.z});
        GameUpdate(&g, &fire, DT);
    }
    CHECK(z->state == ZOMBIE_DYING, "zombie state=%d after four body hits", (int)z->state);
    CHECK(g.player.points == start_points + 3 * 10 + 60, "points=%d expected %d", g.player.points, start_points + 90);
    CHECK(g.kills == 1, "kills=%d", g.kills);
    CHECK(g.player.slots[0].mag == start_mag - 4, "mag=%d expected %d", g.player.slots[0].mag, start_mag - 4);

    FrameInput reload = {.reload_pressed = true};
    int reserve = g.player.slots[0].reserve;
    GameUpdate(&g, &reload, DT);
    CHECK(g.player.reload_timer > 0.0f, "reload_timer=%.3f", (double)g.player.reload_timer);
    CHECK(g.player.slots[0].mag == start_mag - 4, "mag must not refill before reload_time, mag=%d", g.player.slots[0].mag);
    FrameInput idle = {0};
    for (float t = 0.0f; t < WEAPONS[WEAPON_PISTOL].reload_time + 0.1f; t += DT) GameUpdate(&g, &idle, DT);
    CHECK(g.player.slots[0].mag == start_mag, "mag=%d after reload", g.player.slots[0].mag);
    CHECK(g.player.slots[0].reserve == reserve - 4, "reserve=%d expected %d", g.player.slots[0].reserve, reserve - 4);
}

static void TestHeadshotAndWalls(void)
{
    GameState g = NewActiveGame();
    Zombie *z = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 4.0f}, 150.0f);
    FrameInput fire = {.fire_pressed = true};
    int start_points = g.player.points;

    AimAt(&g.player, (Vector3){z->pos.x, 1.65f, z->pos.z});
    GameUpdate(&g, &fire, DT);
    CHECK(z->health == 70.0f, "headshot should deal double damage, health=%.1f", (double)z->health);

    FrameInput idle = {0};
    for (int i = 0; i < 30; i++) GameUpdate(&g, &idle, DT);
    AimAt(&g.player, (Vector3){z->pos.x, 1.65f, z->pos.z});
    GameUpdate(&g, &fire, DT);
    CHECK(z->state == ZOMBIE_DYING, "second headshot should kill, state=%d", (int)z->state);
    CHECK(g.player.points == start_points + 10 + 100, "points=%d expected %d", g.player.points, start_points + 110);

    GameState w = NewActiveGame();
    w.player.pos = (Vector3){5.0f, 0.0f, 3.0f};
    Zombie *behind = PlaceZombie(&w, (Vector3){5.0f, 0.0f, -4.0f}, 150.0f);
    AimAt(&w.player, (Vector3){behind->pos.x, 0.7f, behind->pos.z});
    GameUpdate(&w, &fire, DT);
    CHECK(behind->health == 150.0f, "a wall must block the bullet, health=%.1f", (double)behind->health);
    CHECK(w.player.points == START_POINTS, "points=%d", w.player.points);
}

static Zombie *NearestZombie(GameState *g)
{
    Zombie *nearest = NULL;
    float best = INFINITY;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g->zombies[i];
        if (z->state != ZOMBIE_CHASE && z->state != ZOMBIE_ATTACK) continue;
        float dx = z->pos.x - g->player.pos.x;
        float dz = z->pos.z - g->player.pos.z;
        float d = dx * dx + dz * dz;
        if (d < best) {
            best = d;
            nearest = z;
        }
    }
    return nearest;
}

static void ShootNearest(GameState *g)
{
    FrameInput fire = {.fire_pressed = true};
    Zombie *z = NearestZombie(g);
    if (z != NULL) AimAt(&g->player, (Vector3){z->pos.x, 0.7f, z->pos.z});
    GameUpdate(g, &fire, DT);
}

static bool OverlapsBox(BoundingBox b, Vector3 p, float r)
{
    return p.x + r > b.min.x && p.x - r < b.max.x && p.z + r > b.min.z && p.z - r < b.max.z;
}

static bool InsideSolid(const GameState *g, Vector3 p, float r)
{
    for (int i = 0; i < LEVEL_WALL_COUNT; i++) {
        if (OverlapsBox(LEVEL_WALLS[i], p, r)) return true;
    }
    for (int i = 0; i < LEVEL_DOOR_COUNT; i++) {
        if (!g->zone_open[LEVEL_DOORS[i].opens_zone] && OverlapsBox(LEVEL_DOORS[i].box, p, r)) return true;
    }
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
        if (LEVEL_INTERACTABLES[i].solid && OverlapsBox(LEVEL_INTERACTABLES[i].box, p, r)) return true;
    }
    return false;
}

static bool IsZoneZeroSpawn(Vector3 pos)
{
    for (int i = 0; i < LEVEL_SPAWN_COUNT; i++) {
        if (LEVEL_SPAWNS[i].zone == 0 && LEVEL_SPAWNS[i].pos.x == pos.x && LEVEL_SPAWNS[i].pos.z == pos.z) return true;
    }
    return false;
}

static void TestIntermissionAndSpawning(void)
{
    GameState g;
    GameInit(&g, 5);
    FrameInput idle = {0};
    float elapsed = 0.0f;
    bool zombie_during_intermission = false;

    while (g.phase == PHASE_INTERMISSION && elapsed < INTERMISSION_TIME + 1.0f) {
        GameUpdate(&g, &idle, DT);
        elapsed += DT;
        zombie_during_intermission = zombie_during_intermission || GameAliveZombies(&g) > 0;
    }
    CHECK(g.phase == PHASE_ROUND_ACTIVE, "phase=%d after %.2fs", (int)g.phase, (double)elapsed);
    CHECK(fabsf(elapsed - INTERMISSION_TIME) < 2.0f * DT, "intermission lasted %.3fs", (double)elapsed);
    CHECK(!zombie_during_intermission, "no zombie may exist during intermission");
    CHECK(g.zombies_left_to_spawn == 6, "round 1 spawn budget=%d", g.zombies_left_to_spawn);

    int spawned = 0;
    bool all_in_zone_zero = true;
    Vector3 first_pos = {0};
    for (float t = 0.0f; t < 14.0f; t += DT) {
        ZombieState before[MAX_ZOMBIES];
        for (int i = 0; i < MAX_ZOMBIES; i++) before[i] = g.zombies[i].state;
        g.player.health = g.player.max_health;
        GameUpdate(&g, &idle, DT);
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            if (before[i] != ZOMBIE_FREE || g.zombies[i].state == ZOMBIE_FREE) continue;
            if (spawned == 0) first_pos = g.zombies[i].pos;
            spawned++;
            all_in_zone_zero = all_in_zone_zero && g.zombies[i].zone == 0 && IsZoneZeroSpawn(g.zombies[i].pos);
        }
    }
    CHECK(spawned == 6, "spawned=%d", spawned);
    CHECK(g.zombies_left_to_spawn == 0, "zombies_left_to_spawn=%d", g.zombies_left_to_spawn);
    CHECK(all_in_zone_zero, "every spawn must be a zone 0 spawn point while zone 1 is closed (first at %.1f,%.1f)",
          (double)first_pos.x, (double)first_pos.z);
}

static void TestRoundProgression(void)
{
    GameState g;
    GameInit(&g, 11);
    FrameInput idle = {0};
    while (g.phase == PHASE_INTERMISSION) GameUpdate(&g, &idle, DT);
    int round1_count = g.zombies_left_to_spawn;
    float round1_health = 0.0f;

    for (int i = 0; i < 60 * 120 && g.phase == PHASE_ROUND_ACTIVE; i++) {
        g.player.health = g.player.max_health;
        ShootNearest(&g);
        for (int k = 0; k < MAX_ZOMBIES && round1_health == 0.0f; k++) {
            if (g.zombies[k].state == ZOMBIE_CHASE) round1_health = g.zombies[k].max_health;
        }
    }
    CHECK(g.phase == PHASE_INTERMISSION, "phase=%d after clearing round 1", (int)g.phase);
    CHECK(g.round_number == 2, "round_number=%d", g.round_number);
    CHECK(g.kills == round1_count, "kills=%d expected %d", g.kills, round1_count);
    CHECK(GameAliveZombies(&g) == 0, "alive=%d", GameAliveZombies(&g));

    while (g.phase == PHASE_INTERMISSION) GameUpdate(&g, &idle, DT);
    CHECK(g.zombies_left_to_spawn > round1_count, "round 2 count=%d round 1 count=%d", g.zombies_left_to_spawn, round1_count);
    float round2_health = 0.0f;
    for (int i = 0; i < 60 * 5 && round2_health == 0.0f; i++) {
        GameUpdate(&g, &idle, DT);
        for (int k = 0; k < MAX_ZOMBIES; k++) {
            if (g.zombies[k].state == ZOMBIE_CHASE) round2_health = g.zombies[k].max_health;
        }
    }
    CHECK(round2_health > round1_health, "round 2 health=%.1f round 1 health=%.1f", (double)round2_health, (double)round1_health);
}

static void TestZombiesStayOutOfWalls(void)
{
    GameState g;
    GameInit(&g, 21);
    g.phase = PHASE_ROUND_ACTIVE;
    g.zombies_left_to_spawn = 20;
    FrameInput circle = {.move = {0.0f, 1.0f}, .look = {0.02f, 0.0f}};
    int violations = 0;
    int peak_alive = 0;
    Vector3 where = {0};

    for (int i = 0; i < 60 * 60; i++) {
        g.player.health = g.player.max_health;
        GameUpdate(&g, &circle, DT);
        if (InsideSolid(&g, g.player.pos, PLAYER_RADIUS - 0.001f)) {
            violations++;
            where = g.player.pos;
        }
        for (int k = 0; k < MAX_ZOMBIES; k++) {
            if (g.zombies[k].state == ZOMBIE_FREE || !InsideSolid(&g, g.zombies[k].pos, ZOMBIE_RADIUS - 0.001f)) continue;
            violations++;
            where = g.zombies[k].pos;
        }
        if (GameAliveZombies(&g) > peak_alive) peak_alive = GameAliveZombies(&g);
    }
    CHECK(violations == 0, "%d samples inside a wall, last at %.2f,%.2f", violations, (double)where.x, (double)where.z);
    CHECK(peak_alive >= 10, "the stress run needs a crowd, peak_alive=%d", peak_alive);
}

typedef struct CrossingCase {
    const char *name;
    Vector3 zombie_start;
    Vector3 player_pos;
} CrossingCase;

static void TestZombiesCrossTheDoorway(void)
{
    const CrossingCase cases[] = {
        {"front room to back room", {-8.0f, 0.0f, 12.0f}, {0.0f, 0.0f, -10.0f}},
        {"back room to front room", {8.0f, 0.0f, -14.0f}, {-5.0f, 0.0f, 5.0f}},
        {"hugging the partition from the front", {-9.0f, 0.0f, 1.2f}, {0.0f, 0.0f, -10.0f}},
        {"hugging the partition from the back", {9.0f, 0.0f, -1.2f}, {0.0f, 0.0f, 10.0f}},
    };
    FrameInput idle = {0};

    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        GameState g = NewActiveGame();
        g.zone_open[1] = true;
        g.player.pos = cases[c].player_pos;
        Zombie *z = PlaceZombie(&g, cases[c].zombie_start, 150.0f);
        int violations = 0;
        for (int i = 0; i < 60 * 45; i++) {
            GameUpdate(&g, &idle, DT);
            violations += InsideSolid(&g, z->pos, ZOMBIE_RADIUS - 0.001f);
        }
        float dx = z->pos.x - g.player.pos.x;
        float dz = z->pos.z - g.player.pos.z;
        CHECK(violations == 0, "%s: %d samples inside a wall", cases[c].name, violations);
        CHECK(z->zone == g.player_zone && sqrtf(dx * dx + dz * dz) < 1.2f,
              "%s: zombie must reach the player, zone=%d pos=%.1f,%.1f", cases[c].name, z->zone, (double)z->pos.x, (double)z->pos.z);
    }
}

#define REACH_STEP 0.5f
#define REACH_BUDGET_SECONDS 90

static bool ZombieHurtsPlayer(const GameState *start, int spawn, Vector3 *stuck_at)
{
    GameState g = *start;
    Zombie *z = PlaceZombie(&g, LEVEL_SPAWNS[spawn].pos, 150.0f);
    FrameInput idle = {0};
    for (int i = 0; i < REACH_BUDGET_SECONDS * 60 && g.player.health == start->player.health; i++) GameUpdate(&g, &idle, DT);
    *stuck_at = z->pos;
    return g.player.health < start->player.health;
}

static void TestEveryZombieReachesEveryPlayerSpot(void)
{
    int runs = 0;
    int stuck = 0;
    char first[160] = "";

    for (int door_open = 0; door_open <= 1; door_open++) {
        for (float x = -10.0f; x <= 10.0f; x += REACH_STEP) {
            for (float z = -16.0f; z <= 14.0f; z += REACH_STEP) {
                GameState g = NewActiveGame();
                g.zone_open[1] = door_open;
                g.player.pos = (Vector3){fminf(fmaxf(x, -9.55f), 9.55f), 0.0f, fminf(fmaxf(z, -15.55f), 13.55f)};
                if (GameBlocked(&g, g.player.pos, PLAYER_RADIUS) || (!door_open && g.player.pos.z < 0.0f)) continue;
                for (int s = 0; s < LEVEL_SPAWN_COUNT; s++) {
                    Vector3 stuck_at;
                    if (!g.zone_open[LEVEL_SPAWNS[s].zone]) continue;
                    runs++;
                    if (ZombieHurtsPlayer(&g, s, &stuck_at)) continue;
                    if (stuck++ == 0) {
                        snprintf(first, sizeof first, "door_open=%d player=%.2f,%.2f spawn=%d stuck at %.2f,%.2f", door_open,
                                 (double)g.player.pos.x, (double)g.player.pos.z, s, (double)stuck_at.x, (double)stuck_at.z);
                    }
                }
            }
        }
    }
    CHECK(runs > 10000, "the sweep must cover the level, runs=%d", runs);
    CHECK(stuck == 0, "%d of %d chasing zombies never reached the player, first: %s", stuck, runs, first);
}

static void TestDamageRegenAndGameOver(void)
{
    GameState g = NewActiveGame();
    FrameInput idle = {0};
    Zombie *z = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 7.0f}, 150.0f);
    z->attack_timer = 0.0f;

    for (int i = 0; i < 60 * 10 && g.player.health == PLAYER_BASE_HEALTH; i++) GameUpdate(&g, &idle, DT);
    CHECK(g.player.health == PLAYER_BASE_HEALTH - ZOMBIE_DAMAGE, "health=%.1f after one zombie hit", (double)g.player.health);
    CHECK(g.player.damage_flash > 0.0f, "damage_flash=%.3f", (double)g.player.damage_flash);

    z->state = ZOMBIE_FREE;
    float hurt = g.player.health;
    for (float t = 0.0f; t < REGEN_DELAY - 0.5f; t += DT) GameUpdate(&g, &idle, DT);
    CHECK(g.player.health == hurt, "no regen before the delay, health=%.1f", (double)g.player.health);
    for (float t = 0.0f; t < 1.0f; t += DT) GameUpdate(&g, &idle, DT);
    CHECK(g.player.health > hurt, "regen after the delay, health=%.1f", (double)g.player.health);
    for (float t = 0.0f; t < 5.0f; t += DT) GameUpdate(&g, &idle, DT);
    CHECK(g.player.health == g.player.max_health, "health=%.1f should refill", (double)g.player.health);

    g.player.points = 4321;
    g.player.max_health = PERK_MAX_HEALTH;
    g.player.health = PERK_MAX_HEALTH;
    g.zone_open[1] = true;
    g.round_number = 4;
    g.kills = 17;
    g.player.slots[1] = (WeaponSlot){WEAPON_SMG, 3, 5};
    g.player.slot_count = 2;
    g.player.active = 1;
    for (int i = 0; i < 4; i++) {
        Zombie *mob = PlaceZombie(&g, (Vector3){-1.0f + (float)i * 0.7f, 0.0f, 9.0f}, 9000.0f);
        mob->attack_timer = 0.0f;
    }
    for (int i = 0; i < 60 * 30 && g.phase != PHASE_GAME_OVER; i++) GameUpdate(&g, &idle, DT);
    CHECK(g.phase == PHASE_GAME_OVER, "phase=%d, health=%.1f", (int)g.phase, (double)g.player.health);
    CHECK(g.player.health == 0.0f, "health=%.1f", (double)g.player.health);
    CHECK(g.rounds_survived == 4, "rounds_survived=%d", g.rounds_survived);

    FrameInput restart = {.restart_pressed = true};
    GameUpdate(&g, &restart, DT);
    CHECK(g.phase == PHASE_GAME_OVER, "restart must wait RESTART_DELAY so a reload mash cannot trigger it");
    for (float t = 0.0f; t < RESTART_DELAY; t += DT) GameUpdate(&g, &idle, DT);
    GameUpdate(&g, &restart, DT);

    GameState fresh;
    GameInit(&fresh, 1);
    fresh.rng_state = g.rng_state;
    CHECK(g.phase == PHASE_INTERMISSION, "phase=%d after restart", (int)g.phase);
    CHECK(g.round_number == 1, "round_number=%d", g.round_number);
    CHECK(g.player.points == START_POINTS, "points=%d", g.player.points);
    CHECK(g.player.max_health == PLAYER_BASE_HEALTH && g.player.health == PLAYER_BASE_HEALTH, "health=%.1f/%.1f",
          (double)g.player.health, (double)g.player.max_health);
    CHECK(!g.zone_open[1] && g.zone_open[0], "zones=%d,%d", g.zone_open[0], g.zone_open[1]);
    CHECK(g.player.slot_count == 1 && g.player.slots[0].id == WEAPON_PISTOL, "slot_count=%d", g.player.slot_count);
    CHECK(GameAliveZombies(&g) == 0 && g.kills == 0, "alive=%d kills=%d", GameAliveZombies(&g), g.kills);
    CHECK(memcmp(&g, &fresh, sizeof g) == 0, "restart must leave every field identical to a fresh GameInit");
}

static void TestZombiesCannotHitThroughThePartition(void)
{
    GameState g = NewActiveGame();
    FrameInput idle = {0};
    g.zone_open[1] = true;
    g.player.pos = (Vector3){5.0f, 0.0f, 0.75f};
    Zombie *z = PlaceZombie(&g, (Vector3){5.0f, 0.0f, -0.75f}, 150.0f);
    z->attack_timer = 0.0f;
    bool hit_through_wall = false;

    for (int i = 0; i < 60 * 10 && z->zone == 1; i++) {
        GameUpdate(&g, &idle, DT);
        hit_through_wall = hit_through_wall || (z->zone == 1 && g.player.health < PLAYER_BASE_HEALTH);
    }
    CHECK(z->zone == 0, "zombie never left the back room, zone=%d", z->zone);
    CHECK(!hit_through_wall, "a zombie in the back room hurt a player in the front room");
}

static int FindInteractable(InteractKind kind, int payload)
{
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
        if (LEVEL_INTERACTABLES[i].kind == kind && (payload < 0 || LEVEL_INTERACTABLES[i].payload == payload)) return i;
    }
    return -1;
}

static void StandNear(GameState *g, int interactable)
{
    Vector3 center = BoxCenter(LEVEL_INTERACTABLES[interactable].box);
    float dx = 0.0f - center.x;
    float dz = 7.0f - center.z;
    float length = sqrtf(dx * dx + dz * dz);
    g->player.pos = (Vector3){center.x + dx / length * 1.5f, 0.0f, center.z + dz / length * 1.5f};
}

static void Press(GameState *g, FrameInput in)
{
    GameUpdate(g, &in, DT);
}

static void TestDoor(void)
{
    int door_item = FindInteractable(INTERACT_DOOR, -1);
    const Door *door = &LEVEL_DOORS[LEVEL_INTERACTABLES[door_item].payload];
    Vector3 door_center = BoxCenter(door->box);
    FrameInput interact = {.interact_pressed = true};
    FrameInput fire = {.fire_pressed = true};
    FrameInput idle = {0};

    GameState g = NewActiveGame();
    Zombie *behind = PlaceZombie(&g, (Vector3){0.0f, 0.0f, -4.0f}, 150.0f);
    StandNear(&g, door_item);
    AimAt(&g.player, (Vector3){behind->pos.x, 0.7f, behind->pos.z});

    g.player.points = door->cost - 1;
    Press(&g, interact);
    CHECK(g.player.points == door->cost - 1, "points=%d, buying without enough points must do nothing", g.player.points);
    CHECK(!g.zone_open[1], "door opened without enough points");

    g.player.pos = (Vector3){0.0f, 0.0f, 10.0f};
    g.player.points = door->cost * 3;
    Press(&g, interact);
    CHECK(g.player.points == door->cost * 3 && !g.zone_open[1], "door bought from out of range");

    StandNear(&g, door_item);
    CHECK(GameBlocked(&g, door_center, 0.1f), "closed door must block movement");
    AimAt(&g.player, (Vector3){behind->pos.x, 0.7f, behind->pos.z});
    Press(&g, fire);
    CHECK(behind->health == 150.0f, "closed door must stop bullets, health=%.1f", (double)behind->health);

    g.player.points = door->cost;
    Press(&g, interact);
    CHECK(g.player.points == 0, "points=%d after buying the door", g.player.points);
    CHECK(g.zone_open[1], "zone 1 should be open");
    CHECK(!GameBlocked(&g, door_center, 0.1f), "open door must stop blocking movement");
    for (int i = 0; i < 30; i++) Press(&g, idle);
    AimAt(&g.player, (Vector3){behind->pos.x, 0.7f, behind->pos.z});
    Press(&g, fire);
    CHECK(behind->health < 150.0f, "bullets must pass the open doorway, health=%.1f", (double)behind->health);

    GameState walker = NewActiveGame();
    walker.zone_open[1] = true;
    walker.player.pos = (Vector3){0.0f, 0.0f, 3.0f};
    FrameInput forward = {.move = {0.0f, 1.0f}};
    for (int i = 0; i < 60 * 2; i++) GameUpdate(&walker, &forward, DT);
    CHECK(walker.player.pos.z < -2.0f && walker.player_zone == 1, "player stuck at z=%.2f zone=%d", (double)walker.player.pos.z, walker.player_zone);

    GameState sealed = NewActiveGame();
    sealed.player.pos = (Vector3){0.0f, 0.0f, 3.0f};
    for (int i = 0; i < 60 * 2; i++) GameUpdate(&sealed, &forward, DT);
    CHECK(sealed.player.pos.z > 0.0f && sealed.player_zone == 0, "closed door let the player through, z=%.2f", (double)sealed.player.pos.z);

    GameState spawner = NewActiveGame();
    spawner.zone_open[1] = true;
    spawner.zombies_left_to_spawn = 24;
    bool zone_seen[ZONE_COUNT] = {false};
    for (int i = 0; i < 60 * 45; i++) {
        ZombieState before[MAX_ZOMBIES];
        for (int k = 0; k < MAX_ZOMBIES; k++) before[k] = spawner.zombies[k].state;
        spawner.player.health = spawner.player.max_health;
        GameUpdate(&spawner, &idle, DT);
        for (int k = 0; k < MAX_ZOMBIES; k++) {
            if (before[k] == ZOMBIE_FREE && spawner.zombies[k].state != ZOMBIE_FREE) zone_seen[spawner.zombies[k].zone] = true;
        }
    }
    CHECK(zone_seen[0] && zone_seen[1], "spawns seen per zone: %d,%d", zone_seen[0], zone_seen[1]);
}

static void TestWeaponsAndPerk(void)
{
    int smg_item = FindInteractable(INTERACT_WALL_BUY, WEAPON_SMG);
    int shotgun_item = FindInteractable(INTERACT_WALL_BUY, WEAPON_SHOTGUN);
    int perk_item = FindInteractable(INTERACT_PERK, -1);
    FrameInput interact = {.interact_pressed = true};
    FrameInput swap = {.swap_pressed = true};
    FrameInput idle = {0};
    GameState g = NewActiveGame();

    StandNear(&g, smg_item);
    g.player.points = WEAPONS[WEAPON_SMG].wall_cost - 1;
    Press(&g, interact);
    CHECK(g.player.slot_count == 1 && g.player.points == WEAPONS[WEAPON_SMG].wall_cost - 1, "SMG bought with too few points");

    g.player.points = WEAPONS[WEAPON_SMG].wall_cost;
    Press(&g, interact);
    CHECK(g.player.points == 0, "points=%d", g.player.points);
    CHECK(g.player.slot_count == 2 && g.player.slots[1].id == WEAPON_SMG, "slot_count=%d", g.player.slot_count);
    CHECK(g.player.active == 1, "bought weapon should be active, active=%d", g.player.active);
    CHECK(g.player.slots[1].mag == WEAPONS[WEAPON_SMG].mag_size && g.player.slots[1].reserve == WEAPONS[WEAPON_SMG].reserve_max,
          "SMG ammo=%d/%d", g.player.slots[1].mag, g.player.slots[1].reserve);

    for (int i = 0; i < 20; i++) Press(&g, idle);
    Press(&g, swap);
    CHECK(g.player.active == 0, "swap should select the pistol, active=%d", g.player.active);
    for (int i = 0; i < 30; i++) Press(&g, idle);
    Press(&g, swap);
    CHECK(g.player.active == 1, "swap should return to the SMG, active=%d", g.player.active);

    Zombie *target = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 4.0f}, 9000.0f);
    g.player.pos = (Vector3){0.0f, 0.0f, 10.0f};
    for (int i = 0; i < 30; i++) Press(&g, idle);
    int mag_before = g.player.slots[1].mag;
    FrameInput hold = {.fire_held = true};
    for (int i = 0; i < 60; i++) {
        AimAt(&g.player, (Vector3){target->pos.x, 0.7f, target->pos.z});
        GameUpdate(&g, &hold, DT);
    }
    int expected = (int)(1.0f / WEAPONS[WEAPON_SMG].fire_interval);
    int fired = mag_before - g.player.slots[1].mag;
    CHECK(abs(fired - expected) <= 1, "automatic fire shot %d in one second, table says about %d", fired, expected);

    StandNear(&g, shotgun_item);
    g.player.points = WEAPONS[WEAPON_SHOTGUN].wall_cost;
    Press(&g, interact);
    CHECK(g.player.slot_count == 2 && g.player.slots[1].id == WEAPON_SHOTGUN && g.player.slots[0].id == WEAPON_PISTOL,
          "third weapon must replace the active slot, slots=%d,%d", (int)g.player.slots[0].id, (int)g.player.slots[1].id);

    g.player.pos = (Vector3){0.0f, 0.0f, 10.0f};
    for (int i = 0; i < 60; i++) Press(&g, idle);
    target->health = 9000.0f;
    AimAt(&g.player, (Vector3){target->pos.x, 0.7f, target->pos.z});
    int points_before = g.player.points;
    Press(&g, (FrameInput){.fire_pressed = true});
    CHECK(g.player.slots[1].mag == WEAPONS[WEAPON_SHOTGUN].mag_size - 1, "one trigger pull uses one shell, mag=%d", g.player.slots[1].mag);
    int pellet_hits = g.player.points - points_before;
    CHECK(pellet_hits >= 30 && pellet_hits <= 10 * WEAPONS[WEAPON_SHOTGUN].pellets, "pellet hit points=%d", pellet_hits);
    CHECK(target->health <= 9000.0f - 3.0f * WEAPONS[WEAPON_SHOTGUN].damage, "shotgun damage left health=%.1f", (double)target->health);

    StandNear(&g, shotgun_item);
    g.player.slots[1].mag = 0;
    g.player.slots[1].reserve = 0;
    g.player.points = WEAPONS[WEAPON_SHOTGUN].wall_cost;
    Press(&g, interact);
    CHECK(g.player.slot_count == 2 && g.player.points == 0, "refill should cost the wall price, points=%d", g.player.points);
    CHECK(g.player.slots[1].mag == WEAPONS[WEAPON_SHOTGUN].mag_size && g.player.slots[1].reserve == WEAPONS[WEAPON_SHOTGUN].reserve_max,
          "refilled ammo=%d/%d", g.player.slots[1].mag, g.player.slots[1].reserve);

    StandNear(&g, perk_item);
    g.player.health = 30.0f;
    g.player.points = 2499;
    Press(&g, interact);
    CHECK(!PlayerHasPerk(&g.player) && g.player.points == 2499, "perk bought with 2499 points");
    g.player.points = LEVEL_INTERACTABLES[perk_item].payload;
    Press(&g, interact);
    CHECK(g.player.max_health == PERK_MAX_HEALTH, "max_health=%.1f", (double)g.player.max_health);
    CHECK(g.player.health == PERK_MAX_HEALTH, "perk should heal to the new max, health=%.1f", (double)g.player.health);
    CHECK(g.player.points == 0, "points=%d", g.player.points);
    g.player.points = 5000;
    Press(&g, interact);
    CHECK(g.player.points == 5000, "a second perk purchase must be refused, points=%d", g.player.points);
    CHECK(GameNearestInteractable(&g) != perk_item, "owned perk must not show a prompt");
}

static void TestSwapCannotShortenTheFireCooldown(void)
{
    const float interval = WEAPONS[WEAPON_SHOTGUN].fire_interval;
    GameState g = NewActiveGame();
    g.player.slots[1] = (WeaponSlot){WEAPON_SHOTGUN, WEAPONS[WEAPON_SHOTGUN].mag_size, WEAPONS[WEAPON_SHOTGUN].reserve_max};
    g.player.slot_count = 2;
    g.player.active = 1;
    FrameInput fire = {.fire_pressed = true};
    FrameInput swap = {.swap_pressed = true};

    Press(&g, fire);
    int shells = g.player.slots[1].mag;
    Press(&g, swap);
    Press(&g, swap);
    CHECK(g.player.active == 1, "two swaps must return to the shotgun, active=%d", g.player.active);
    float elapsed = 2.0f * DT;
    while (g.player.slots[1].mag == shells && elapsed < 2.0f) {
        Press(&g, fire);
        elapsed += DT;
    }
    CHECK(g.player.slots[1].mag == shells - 1, "the shotgun never fired again, mag=%d", g.player.slots[1].mag);
    CHECK(elapsed >= interval - DT, "double swap fired %.2fs after a shot, fire_interval is %.2fs", (double)elapsed, (double)interval);

    GameState idle = NewActiveGame();
    idle.player.slots[1] = (WeaponSlot){WEAPON_SHOTGUN, WEAPONS[WEAPON_SHOTGUN].mag_size, WEAPONS[WEAPON_SHOTGUN].reserve_max};
    idle.player.slot_count = 2;
    Press(&idle, swap);
    Press(&idle, fire);
    CHECK(idle.player.slots[1].mag == WEAPONS[WEAPON_SHOTGUN].mag_size, "a weapon must not fire on the frame it is drawn");
}

static void TestFullAmmoRefillIsNotSold(void)
{
    int smg_item = FindInteractable(INTERACT_WALL_BUY, WEAPON_SMG);
    FrameInput interact = {.interact_pressed = true};
    FrameInput fire = {.fire_held = true};
    FrameInput idle = {0};
    GameState g = NewActiveGame();

    StandNear(&g, smg_item);
    g.player.points = WEAPONS[WEAPON_SMG].wall_cost + 500;
    Press(&g, interact);
    CHECK(g.player.slot_count == 2 && g.player.points == 500, "SMG purchase, slots=%d points=%d", g.player.slot_count, g.player.points);

    g.player.points = 5000;
    Press(&g, interact);
    CHECK(g.player.points == 5000, "a full SMG must not be refilled for points, points=%d", g.player.points);
    CHECK(GameNearestInteractable(&g) != smg_item, "no prompt may offer ammo that is already full");

    g.player.slots[1].reserve--;
    CHECK(GameNearestInteractable(&g) == smg_item, "missing reserve ammo must bring the prompt back");
    Press(&g, interact);
    CHECK(g.player.points == 5000 - WEAPONS[WEAPON_SMG].wall_cost && g.player.slots[1].reserve == WEAPONS[WEAPON_SMG].reserve_max,
          "refill points=%d reserve=%d", g.player.points, g.player.slots[1].reserve);

    for (int i = 0; i < 30; i++) Press(&g, idle);
    Press(&g, fire);
    CHECK(g.player.slots[1].mag == WEAPONS[WEAPON_SMG].mag_size - 1, "one round fired, mag=%d", g.player.slots[1].mag);
    int points = g.player.points;
    Press(&g, interact);
    CHECK(g.player.points == points - WEAPONS[WEAPON_SMG].wall_cost && g.player.slots[1].mag == WEAPONS[WEAPON_SMG].mag_size,
          "an empty magazine slot must be refillable, points=%d mag=%d", g.player.points, g.player.slots[1].mag);
}

static void TestGameOverLetsDyingZombiesFinishFalling(void)
{
    GameState g = NewActiveGame();
    FrameInput fire = {.fire_pressed = true};
    FrameInput idle = {0};
    Zombie *dying = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 4.0f}, 40.0f);
    Zombie *flashing = PlaceZombie(&g, (Vector3){3.0f, 0.0f, 4.0f}, 150.0f);

    AimAt(&g.player, (Vector3){dying->pos.x, 0.7f, dying->pos.z});
    Press(&g, fire);
    for (int i = 0; i < 20; i++) Press(&g, idle);
    AimAt(&g.player, (Vector3){flashing->pos.x, 0.7f, flashing->pos.z});
    Press(&g, fire);
    CHECK(dying->state == ZOMBIE_DYING && flashing->hit_flash > 0.0f, "setup: dying state=%d flash=%.3f", (int)dying->state,
          (double)flashing->hit_flash);

    g.player.health = 0.0f;
    g.phase = PHASE_GAME_OVER;
    g.phase_timer = 0.0f;
    for (int i = 0; i < 60; i++) Press(&g, idle);
    CHECK(dying->state == ZOMBIE_FREE, "a dying zombie froze at game over, state=%d timer=%.3f", (int)dying->state, (double)dying->dying_timer);
    CHECK(flashing->hit_flash == 0.0f, "a hit flash stayed red at game over, hit_flash=%.3f", (double)flashing->hit_flash);
}

#define BODY_CONTACT (PLAYER_RADIUS + ZOMBIE_RADIUS)

static float PlanarDistance(Vector3 a, Vector3 b)
{
    return hypotf(a.x - b.x, a.z - b.z);
}

static void Invulnerable(GameState *g)
{
    g->player.max_health = 1e6f;
    g->player.health = 1e6f;
}

static void TestPlayerCannotWalkThroughZombies(void)
{
    GameState g = NewActiveGame();
    Invulnerable(&g);
    Zombie *z = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 7.0f}, 150.0f);
    FrameInput forward = {.move = {0.0f, 1.0f}};
    float closest = INFINITY;

    for (int i = 0; i < 60 * 2; i++) {
        GameUpdate(&g, &forward, DT);
        closest = fminf(closest, PlanarDistance(g.player.pos, z->pos));
    }
    CHECK(closest >= BODY_CONTACT - 0.001f, "player walked into a zombie, closest=%.2f", (double)closest);
    CHECK(g.player.pos.z > z->pos.z, "player passed through the zombie, player z=%.2f zombie z=%.2f", (double)g.player.pos.z, (double)z->pos.z);

    GameState pinned = NewActiveGame();
    Invulnerable(&pinned);
    Zombie *on_top = PlaceZombie(&pinned, (Vector3){0.0f, 0.0f, 9.7f}, 150.0f);
    FrameInput back = {.move = {0.0f, -1.0f}};
    for (int i = 0; i < 60; i++) GameUpdate(&pinned, &back, DT);
    CHECK(PlanarDistance(pinned.player.pos, on_top->pos) >= BODY_CONTACT, "a player overlapping a zombie must be able to walk away, distance=%.2f",
          (double)PlanarDistance(pinned.player.pos, on_top->pos));
    CHECK(pinned.player.pos.z > 11.0f, "walking away stalled at z=%.2f", (double)pinned.player.pos.z);
}

static void TestZombiesNeverStackOnThePlayer(void)
{
    GameState g = NewActiveGame();
    g.player.pos = (Vector3){0.0f, 0.0f, 7.0f};
    for (int i = 0; i < 14; i++) {
        float angle = (float)i * 0.9f;
        float radius = 2.0f + (float)(i % 4);
        PlaceZombie(&g, (Vector3){radius * sinf(angle), 0.0f, 7.0f + radius * cosf(angle)}, 9000.0f);
    }
    FrameInput idle = {0};
    float closest = INFINITY;
    bool hurt = false;

    for (int i = 0; i < 60 * 25; i++) {
        GameUpdate(&g, &idle, DT);
        hurt = hurt || g.player.health < g.player.max_health;
        g.player.health = g.player.max_health;
        for (int k = 0; k < MAX_ZOMBIES; k++) {
            if (g.zombies[k].state == ZOMBIE_FREE) continue;
            closest = fminf(closest, PlanarDistance(g.player.pos, g.zombies[k].pos));
        }
    }
    CHECK(closest >= BODY_CONTACT - 0.001f, "a zombie reached %.2fm from the player, bodies touch at %.2fm", (double)closest, (double)BODY_CONTACT);
    CHECK(hurt, "zombies around the player must still land hits");
}

static const char *InteractableName(int index)
{
    switch (LEVEL_INTERACTABLES[index].kind) {
    case INTERACT_WALL_BUY: return WEAPONS[LEVEL_INTERACTABLES[index].payload].name;
    case INTERACT_DOOR: return "door";
    case INTERACT_PERK: return "perk";
    }
    return "?";
}

static void TestInteractablesAreVisibleInPlainView(void)
{
    /* The last three eyes stand at touching distance of the door and the perk, where a ray to a point above the box clips the box. */
    const Vector3 eyes[] = {
        {0.0f, PLAYER_EYE_HEIGHT, 10.0f}, {0.0f, PLAYER_EYE_HEIGHT, 8.0f}, {-5.0f, PLAYER_EYE_HEIGHT, 4.0f},
        {5.0f, PLAYER_EYE_HEIGHT, 12.0f}, {-9.0f, PLAYER_EYE_HEIGHT, 2.0f}, {0.0f, PLAYER_EYE_HEIGHT, 0.8f},
        {-1.0f, PLAYER_EYE_HEIGHT, 1.0f}, {9.2f, PLAYER_EYE_HEIGHT, 12.2f},
    };
    GameState g = NewActiveGame();

    for (size_t e = 0; e < sizeof eyes / sizeof eyes[0]; e++) {
        for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
            CHECK(GameInteractableVisible(&g, i, eyes[e]), "%s must be visible from (%.1f, %.1f) in the front room", InteractableName(i),
                  (double)eyes[e].x, (double)eyes[e].z);
        }
    }
}

static void TestPartitionHidesTheFrontRoomFromTheBackRoom(void)
{
    const Vector3 eyes[] = {
        {8.0f, PLAYER_EYE_HEIGHT, -2.0f}, {-8.0f, PLAYER_EYE_HEIGHT, -3.0f}, {0.0f, PLAYER_EYE_HEIGHT, -8.0f},
    };
    GameState g = NewActiveGame();

    for (int door_open = 0; door_open <= 1; door_open++) {
        g.zone_open[1] = door_open != 0;
        for (size_t e = 0; e < sizeof eyes / sizeof eyes[0]; e++) {
            for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
                if (LEVEL_INTERACTABLES[i].kind == INTERACT_DOOR) continue;
                CHECK(!GameInteractableVisible(&g, i, eyes[e]), "%s must stay hidden from (%.1f, %.1f) behind the partition, door open=%d",
                      InteractableName(i), (double)eyes[e].x, (double)eyes[e].z, door_open);
            }
        }
    }
}

static void TestDoorwayControlsTheViewIntoTheFrontRoom(void)
{
    int perk_item = FindInteractable(INTERACT_PERK, -1);
    Vector3 lined_up = {-3.0f, PLAYER_EYE_HEIGHT, -3.0f};
    GameState g = NewActiveGame();

    CHECK(!GameInteractableVisible(&g, perk_item, lined_up), "a closed door must block the view of the perk");
    g.zone_open[1] = true;
    CHECK(GameInteractableVisible(&g, perk_item, lined_up), "an open doorway must show the perk");
}

int main(void)
{
    TestInit();
    TestShootingAndReload();
    TestHeadshotAndWalls();
    TestIntermissionAndSpawning();
    TestRoundProgression();
    TestZombiesStayOutOfWalls();
    TestZombiesCrossTheDoorway();
    TestEveryZombieReachesEveryPlayerSpot();
    TestDamageRegenAndGameOver();
    TestZombiesCannotHitThroughThePartition();
    TestDoor();
    TestWeaponsAndPerk();
    TestSwapCannotShortenTheFireCooldown();
    TestFullAmmoRefillIsNotSold();
    TestGameOverLetsDyingZombiesFinishFalling();
    TestPlayerCannotWalkThroughZombies();
    TestZombiesNeverStackOnThePlayer();
    TestInteractablesAreVisibleInPlainView();
    TestPartitionHidesTheFrontRoomFromTheBackRoom();
    TestDoorwayControlsTheViewIntoTheFrontRoom();

    if (g_failures > 0) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return EXIT_FAILURE;
    }
    printf("all %d checks passed\n", g_checks);
    return EXIT_SUCCESS;
}
