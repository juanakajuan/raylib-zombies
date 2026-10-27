#define _POSIX_C_SOURCE 200112L

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "game.h"
#include "settings.h"

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
    g.player.perks = (uint8_t)((1u << PERK_VITALITY) | (1u << PERK_QUICK_HANDS));
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
    CHECK(!PlayerHasPerk(&g.player, PERK_VITALITY) && !PlayerHasPerk(&g.player, PERK_QUICK_HANDS), "perks=%d should reset", g.player.perks);
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

static int FindWallBuy(WeaponId id)
{
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
        if (LEVEL_INTERACTABLES[i].kind == INTERACT_WALL_BUY && LEVEL_INTERACTABLES[i].weapon == id) return i;
    }
    return -1;
}

static int FindPerk(PerkId id)
{
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
        if (LEVEL_INTERACTABLES[i].kind == INTERACT_PERK && LEVEL_INTERACTABLES[i].perk == id) return i;
    }
    return -1;
}

static int FindDoor(void)
{
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
        if (LEVEL_INTERACTABLES[i].kind == INTERACT_DOOR) return i;
    }
    return -1;
}

static bool InBackRoom(int index)
{
    return LEVEL_INTERACTABLES[index].box.max.z <= 0.0f;
}

static bool InFrontRoom(int index)
{
    return LEVEL_INTERACTABLES[index].box.min.z >= 0.0f;
}

static void StandNear(GameState *g, int interactable)
{
    Vector3 center = BoxCenter(LEVEL_INTERACTABLES[interactable].box);
    float dx = 0.0f - center.x;
    float dz = (center.z < 0.0f ? -8.0f : 7.0f) - center.z;
    float length = sqrtf(dx * dx + dz * dz);
    g->player.pos = (Vector3){center.x + dx / length * 1.5f, 0.0f, center.z + dz / length * 1.5f};
}

static void Press(GameState *g, FrameInput in)
{
    GameUpdate(g, &in, DT);
}

static void TestDoor(void)
{
    int door_item = FindDoor();
    const Door *door = &LEVEL_DOORS[LEVEL_INTERACTABLES[door_item].door];
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
    int smg_item = FindWallBuy(WEAPON_SMG);
    int shotgun_item = FindWallBuy(WEAPON_SHOTGUN);
    int perk_item = FindPerk(PERK_VITALITY);
    FrameInput interact = {.interact_pressed = true};
    FrameInput select_first = {.select_slot = 1};
    FrameInput select_second = {.select_slot = 2};
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
    Press(&g, select_first);
    CHECK(g.player.active == 0, "key 1 should select the pistol, active=%d", g.player.active);
    for (int i = 0; i < 30; i++) Press(&g, idle);
    Press(&g, select_second);
    CHECK(g.player.active == 1, "key 2 should return to the SMG, active=%d", g.player.active);

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
    CHECK(!PlayerHasPerk(&g.player, PERK_VITALITY) && g.player.points == 2499, "perk bought with 2499 points");
    g.player.points = PERKS[PERK_VITALITY].cost;
    Press(&g, interact);
    CHECK(PlayerHasPerk(&g.player, PERK_VITALITY), "Vitality Brew should be owned after the purchase");
    CHECK(g.player.max_health == PERK_MAX_HEALTH, "max_health=%.1f", (double)g.player.max_health);
    CHECK(g.player.health == PERK_MAX_HEALTH, "perk should heal to the new max, health=%.1f", (double)g.player.health);
    CHECK(g.player.points == 0, "points=%d", g.player.points);
    g.player.points = 5000;
    Press(&g, interact);
    CHECK(g.player.points == 5000, "a second perk purchase must be refused, points=%d", g.player.points);
    CHECK(GameNearestInteractable(&g) != perk_item, "owned perk must not show a prompt");
}

static int FramesToReload(GameState *g)
{
    FrameInput idle = {0};
    int frames = 0;
    g->player.slots[g->player.active].mag = 0;
    Press(g, (FrameInput){.reload_pressed = true});
    while (g->player.reload_timer > 0.0f && frames < 60 * 10) {
        GameUpdate(g, &idle, DT);
        frames++;
    }
    return frames;
}

static void TestQuickHandsPerk(void)
{
    int item = FindPerk(PERK_QUICK_HANDS);
    CHECK(item >= 0, "the level must hold a Quick Hands machine");
    if (item < 0) return;
    CHECK(InBackRoom(item), "Quick Hands belongs behind the door");

    FrameInput interact = {.interact_pressed = true};
    GameState g = NewActiveGame();
    g.zone_open[1] = true;
    StandNear(&g, item);
    g.player.points = PERKS[PERK_QUICK_HANDS].cost - 1;
    Press(&g, interact);
    CHECK(!PlayerHasPerk(&g.player, PERK_QUICK_HANDS) && g.player.points == PERKS[PERK_QUICK_HANDS].cost - 1,
          "Quick Hands bought with 1 point too few, points=%d", g.player.points);

    const float reload_time = WEAPONS[WEAPON_PISTOL].reload_time;
    GameState plain = g;
    int plain_frames = FramesToReload(&plain);
    CHECK(fabsf((float)plain_frames * DT - reload_time) < 0.1f, "a plain reload took %.2fs, table says %.2fs", (double)plain_frames * DT, (double)reload_time);

    g.player.points = PERKS[PERK_QUICK_HANDS].cost;
    Press(&g, interact);
    CHECK(PlayerHasPerk(&g.player, PERK_QUICK_HANDS) && !PlayerHasPerk(&g.player, PERK_VITALITY), "perks=%d", g.player.perks);
    CHECK(g.player.points == 0, "points=%d", g.player.points);
    CHECK(g.player.max_health == PLAYER_BASE_HEALTH, "Quick Hands must not touch max_health, got %.1f", (double)g.player.max_health);
    CHECK(GameNearestInteractable(&g) != item, "an owned perk must not show a prompt");

    g.player.points = 5000;
    Press(&g, interact);
    CHECK(g.player.points == 5000, "a second Quick Hands purchase must be refused, points=%d", g.player.points);

    int quick_frames = FramesToReload(&g);
    CHECK(fabsf((float)quick_frames * DT - 0.5f * reload_time) < 0.1f, "a Quick Hands reload took %.2fs, expected %.2fs", (double)quick_frames * DT,
          (double)(0.5f * reload_time));
    CHECK(g.player.slots[0].mag == WEAPONS[WEAPON_PISTOL].mag_size, "the quick reload must fill the magazine, mag=%d", g.player.slots[0].mag);
}

static void TestQuickHandsCannotTrapZombies(void)
{
    int item = FindPerk(PERK_QUICK_HANDS);
    if (item < 0) return;
    int reached = 0;
    int runs = 0;
    GameState g = NewActiveGame();
    g.zone_open[1] = true;
    StandNear(&g, item);
    for (int s = 0; s < LEVEL_SPAWN_COUNT; s++) {
        Vector3 stuck_at;
        runs++;
        reached += ZombieHurtsPlayer(&g, s, &stuck_at);
    }
    CHECK(runs > 0 && reached == runs, "%d of %d zombies reached a player standing next to Quick Hands", reached, runs);
}

static void TestSwapCannotShortenTheFireCooldown(void)
{
    const float interval = WEAPONS[WEAPON_SHOTGUN].fire_interval;
    GameState g = NewActiveGame();
    g.player.slots[1] = (WeaponSlot){WEAPON_SHOTGUN, WEAPONS[WEAPON_SHOTGUN].mag_size, WEAPONS[WEAPON_SHOTGUN].reserve_max};
    g.player.slot_count = 2;
    g.player.active = 1;
    FrameInput fire = {.fire_pressed = true};
    FrameInput swap = {.cycle_steps = 1};

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

static void TestWeaponSelectionInput(void)
{
    FrameInput idle = {0};
    GameState g = NewActiveGame();
    g.player.slots[1] = (WeaponSlot){WEAPON_SHOTGUN, WEAPONS[WEAPON_SHOTGUN].mag_size, WEAPONS[WEAPON_SHOTGUN].reserve_max};
    g.player.slot_count = 2;

    Press(&g, (FrameInput){.cycle_steps = 1});
    CHECK(g.player.active == 1, "wheel down should select the next weapon, active=%d", g.player.active);
    Press(&g, (FrameInput){.cycle_steps = 1});
    CHECK(g.player.active == 0, "wheel down past the last slot should wrap to the first, active=%d", g.player.active);
    Press(&g, (FrameInput){.cycle_steps = -1});
    CHECK(g.player.active == 1, "wheel up from the first slot should wrap to the last, active=%d", g.player.active);
    Press(&g, (FrameInput){.select_slot = 3});
    CHECK(g.player.active == 1, "a number key for an empty slot must not switch, active=%d", g.player.active);

    for (int i = 0; i < 30; i++) Press(&g, idle);
    g.player.slots[1].mag = 0;
    Press(&g, (FrameInput){.reload_pressed = true});
    CHECK(g.player.reload_timer > 0.0f, "reload should start");
    Press(&g, (FrameInput){.select_slot = 2});
    CHECK(g.player.reload_timer > 0.0f, "selecting the weapon in hand must not cancel its reload");

    GameState lone = NewActiveGame();
    Press(&lone, (FrameInput){.cycle_steps = 1});
    Press(&lone, (FrameInput){.select_slot = 2});
    CHECK(lone.player.active == 0, "with one weapon nothing should switch, active=%d", lone.player.active);
}

static void TestFullAmmoRefillIsNotSold(void)
{
    int smg_item = FindWallBuy(WEAPON_SMG);
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

static void TestJump(void)
{
    GameState g = NewActiveGame();
    float ground_eye = PlayerEye(&g.player).y;
    FrameInput jump = {.jump_pressed = true};
    FrameInput idle = {0};
    float peak = ground_eye;

    GameUpdate(&g, &jump, DT);
    for (int i = 0; i < 60; i++) {
        GameUpdate(&g, i == 10 ? &jump : &idle, DT);
        peak = fmaxf(peak, PlayerEye(&g.player).y);
    }
    CHECK(peak > ground_eye + 0.5f, "jump barely lifted the eye, peak=%.2f", (double)peak);
    CHECK(peak < ground_eye + 1.0f, "jump went too high, peak=%.2f", (double)peak);
    CHECK(PlayerEye(&g.player).y == ground_eye, "player did not land, eye=%.2f", (double)PlayerEye(&g.player).y);
    CHECK(g.player.pos.y == 0.0f, "feet below or above the floor, y=%.2f", (double)g.player.pos.y);
}

static void TestNoDoubleJump(void)
{
    GameState g = NewActiveGame();
    FrameInput jump = {.jump_pressed = true};
    FrameInput idle = {0};
    GameUpdate(&g, &jump, DT);
    for (int i = 0; i < 25; i++) GameUpdate(&g, &idle, DT);
    float before = g.player.pos.y;
    GameUpdate(&g, &jump, DT);
    CHECK(g.player.pos.y < before, "a mid-air jump press pushed the player up, before=%.2f after=%.2f", (double)before, (double)g.player.pos.y);

    float peak = 0.0f;
    for (int i = 0; i < 60; i++) {
        GameUpdate(&g, &jump, DT);
        peak = fmaxf(peak, g.player.pos.y);
    }
    CHECK(peak < 0.9f, "repeated presses stacked into a higher jump, peak=%.2f", (double)peak);
}

static void TestCrouch(void)
{
    GameState g = NewActiveGame();
    FrameInput crouch = {.crouch_held = true};
    FrameInput idle = {0};

    GameUpdate(&g, &crouch, DT);
    CHECK(PlayerEye(&g.player).y > CROUCH_EYE_HEIGHT + 0.3f, "crouch snapped instantly, eye=%.2f", (double)PlayerEye(&g.player).y);
    for (int i = 0; i < 60; i++) GameUpdate(&g, &crouch, DT);
    CHECK(fabsf(PlayerEye(&g.player).y - CROUCH_EYE_HEIGHT) < 0.001f, "eye=%.2f", (double)PlayerEye(&g.player).y);
    for (int i = 0; i < 60; i++) GameUpdate(&g, &idle, DT);
    CHECK(fabsf(PlayerEye(&g.player).y - PLAYER_EYE_HEIGHT) < 0.001f, "eye after release=%.2f", (double)PlayerEye(&g.player).y);

    GameState walk = NewActiveGame();
    GameState sneak = NewActiveGame();
    FrameInput forward = {.move = {0.0f, 1.0f}};
    FrameInput crouch_forward = {.move = {0.0f, 1.0f}, .crouch_held = true, .sprint_held = true};
    for (int i = 0; i < 30; i++) {
        GameUpdate(&walk, &forward, DT);
        GameUpdate(&sneak, &crouch_forward, DT);
    }
    float walked = PlanarDistance(walk.player.pos, (Vector3){0.0f, 0.0f, 10.0f});
    float crept = PlanarDistance(sneak.player.pos, (Vector3){0.0f, 0.0f, 10.0f});
    CHECK(crept > 0.5f && crept < walked * 0.6f, "crouch walk=%.2f normal walk=%.2f", (double)crept, (double)walked);
}

static void TestCannotJumpWhileCrouched(void)
{
    GameState g = NewActiveGame();
    FrameInput crouch_jump = {.crouch_held = true, .jump_pressed = true};
    for (int i = 0; i < 10; i++) GameUpdate(&g, &crouch_jump, DT);
    CHECK(g.player.pos.y == 0.0f, "crouched player jumped, y=%.2f", (double)g.player.pos.y);
}

#define SLAB_FRONT_X (-8.2f)

static GameState NewGameBeforeSlab(void)
{
    GameState g = NewActiveGame();
    g.player.pos = (Vector3){SLAB_FRONT_X, 0.0f, 10.0f};
    return g;
}

static GameState NewGameUnderSlab(void)
{
    GameState g = NewGameBeforeSlab();
    FrameInput crouch_forward = {.move = {0.0f, 1.0f}, .crouch_held = true};
    while (g.player.pos.z > 3.8f) GameUpdate(&g, &crouch_forward, DT);
    return g;
}

static void TestStandingPlayerCannotWalkUnderTheSlab(void)
{
    GameState g = NewGameBeforeSlab();
    FrameInput forward = {.move = {0.0f, 1.0f}};
    for (int i = 0; i < 60 * 5; i++) GameUpdate(&g, &forward, DT);
    float edge = LEVEL_OVERHANGS[0].max.z + PLAYER_RADIUS;
    CHECK(g.player.pos.z > edge - 0.01f, "standing player entered the slab, z=%.2f", (double)g.player.pos.z);
    CHECK(g.player.pos.z < edge + 0.1f, "standing player stopped short of the slab, z=%.2f", (double)g.player.pos.z);
}

static void TestCrouchedPlayerWalksUnderTheSlab(void)
{
    GameState g = NewGameUnderSlab();
    CHECK(g.player.pos.z < 4.0f && g.player.pos.z > LEVEL_OVERHANGS[0].min.z - PLAYER_RADIUS,
          "crouched player did not get under the slab, z=%.2f", (double)g.player.pos.z);
}

static void TestReleasingCrouchUnderTheSlabKeepsThePlayerLow(void)
{
    GameState g = NewGameUnderSlab();
    g.player.pos.z = 4.2f;
    FrameInput forward = {.move = {0.0f, 1.0f}, .jump_pressed = true};
    for (int i = 0; i < 12; i++) GameUpdate(&g, &forward, DT);
    CHECK(fabsf(PlayerEye(&g.player).y - CROUCH_EYE_HEIGHT) < 0.001f, "eye rose under the slab, eye=%.2f", (double)PlayerEye(&g.player).y);
    CHECK(g.player.pos.y == 0.0f, "player jumped under the slab, y=%.2f", (double)g.player.pos.y);
    float crept = 4.2f - g.player.pos.z;
    CHECK(fabsf(crept - 12.0f * DT * 2.2f) < 0.01f, "player did not move at crouch speed, moved %.2f", (double)crept);

    FrameInput out = {.move = {0.0f, 1.0f}};
    for (int i = 0; i < 120; i++) GameUpdate(&g, &out, DT);
    CHECK(g.player.pos.z < LEVEL_OVERHANGS[0].min.z - PLAYER_RADIUS, "player stayed under the slab, z=%.2f", (double)g.player.pos.z);
    CHECK(fabsf(PlayerEye(&g.player).y - PLAYER_EYE_HEIGHT) < 0.001f, "eye did not return, eye=%.2f", (double)PlayerEye(&g.player).y);
}

static float ShotAtZombieBehind(float x)
{
    GameState g = NewActiveGame();
    g.player.pos = (Vector3){x, 0.0f, 6.0f};
    Zombie *z = PlaceZombie(&g, (Vector3){x, 0.0f, 3.5f}, 150.0f);
    AimAt(&g.player, (Vector3){x, 1.4f, 3.5f});
    FrameInput fire = {.fire_pressed = true};
    GameUpdate(&g, &fire, DT);
    return z->health;
}

static void TestTheSlabStopsBullets(void)
{
    CHECK(ShotAtZombieBehind(-5.0f) < 150.0f, "control shot missed the zombie");
    CHECK(ShotAtZombieBehind(SLAB_FRONT_X) == 150.0f, "a bullet passed through the slab");
}

static void TestZombiesReachPlayersAtTheSlab(void)
{
    const float spots[] = {5.4f, 3.8f};
    for (size_t i = 0; i < sizeof spots / sizeof spots[0]; i++) {
        GameState g = NewGameBeforeSlab();
        g.player.pos.z = spots[i];
        for (int s = 0; s < LEVEL_SPAWN_COUNT; s++) {
            Vector3 stuck_at;
            if (!g.zone_open[LEVEL_SPAWNS[s].zone]) continue;
            CHECK(ZombieHurtsPlayer(&g, s, &stuck_at), "spawn %d never reached the player at z=%.1f, stuck at %.2f,%.2f", s,
                  (double)spots[i], (double)stuck_at.x, (double)stuck_at.z);
        }
    }
}

static void TestJumpDoesNotDodgeZombieAttacks(void)
{
    GameState g = NewActiveGame();
    PlaceZombie(&g, (Vector3){0.0f, 0.0f, 9.0f}, 150.0f)->attack_timer = 0.0f;
    FrameInput jump = {.jump_pressed = true};
    FrameInput idle = {0};
    GameUpdate(&g, &jump, DT);
    for (int i = 0; i < 40; i++) GameUpdate(&g, &idle, DT);
    CHECK(g.player.health < PLAYER_BASE_HEALTH, "a jump dodged the attack, health=%.1f", (double)g.player.health);
}

static void TestZombiesStayOnTheFloorWhilePlayerJumps(void)
{
    GameState g = NewActiveGame();
    Invulnerable(&g);
    PlaceZombie(&g, (Vector3){0.0f, 0.0f, 4.0f}, 150.0f);
    FrameInput jump = {.jump_pressed = true};
    float closest = INFINITY;
    for (int i = 0; i < 60 * 8; i++) {
        GameUpdate(&g, &jump, DT);
        for (int j = 0; j < MAX_ZOMBIES; j++) {
            if (g.zombies[j].state == ZOMBIE_FREE) continue;
            CHECK(g.zombies[j].pos.y == 0.0f, "zombie left the floor, y=%.3f", (double)g.zombies[j].pos.y);
            closest = fminf(closest, PlanarDistance(g.zombies[j].pos, g.player.pos));
        }
    }
    CHECK(closest < 1.3f, "zombie never reached the jumping player, closest=%.2f", (double)closest);
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
    case INTERACT_WALL_BUY: return WEAPONS[LEVEL_INTERACTABLES[index].weapon].name;
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
            if (InBackRoom(i)) continue;
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
                if (!InFrontRoom(i)) continue;
                CHECK(!GameInteractableVisible(&g, i, eyes[e]), "%s must stay hidden from (%.1f, %.1f) behind the partition, door open=%d",
                      InteractableName(i), (double)eyes[e].x, (double)eyes[e].z, door_open);
            }
        }
    }
}

static void TestDoorwayControlsTheViewIntoTheFrontRoom(void)
{
    int perk_item = FindPerk(PERK_VITALITY);
    Vector3 lined_up = {-3.0f, PLAYER_EYE_HEIGHT, -3.0f};
    GameState g = NewActiveGame();

    CHECK(!GameInteractableVisible(&g, perk_item, lined_up), "a closed door must block the view of the perk");
    g.zone_open[1] = true;
    CHECK(GameInteractableVisible(&g, perk_item, lined_up), "an open doorway must show the perk");
}

static int CountEvents(const GameState *g, GameEventKind kind)
{
    int count = 0;
    for (int i = 0; i < g->event_count; i++) count += g->events[i].kind == kind;
    return count;
}

static const GameEvent *FindEvent(const GameState *g, GameEventKind kind)
{
    for (int i = 0; i < g->event_count; i++) {
        if (g->events[i].kind == kind) return &g->events[i];
    }
    return NULL;
}

/* Steps until the event shows up in a step's queue, up to max_steps. */
static bool EventWithin(GameState *g, FrameInput in, GameEventKind kind, int max_steps)
{
    for (int i = 0; i < max_steps; i++) {
        GameUpdate(g, &in, DT);
        if (CountEvents(g, kind) > 0) return true;
    }
    return false;
}

static void TestFireAndReloadEvents(void)
{
    FrameInput fire = {.fire_pressed = true, .fire_held = true};
    FrameInput idle = {0};
    FrameInput reload = {.reload_pressed = true};

    for (int id = 0; id < WEAPON_COUNT; id++) {
        GameState g = NewActiveGame();
        g.player.slots[0] = (WeaponSlot){(WeaponId)id, WEAPONS[id].mag_size, WEAPONS[id].reserve_max};
        Press(&g, fire);
        const GameEvent *e = FindEvent(&g, EVENT_FIRE);
        CHECK(CountEvents(&g, EVENT_FIRE) == 1 && e != NULL && e->arg == id, "weapon %d fire events=%d", id, CountEvents(&g, EVENT_FIRE));
        Press(&g, idle);
        CHECK(CountEvents(&g, EVENT_FIRE) == 0, "weapon %d: the queue must be empty on the next step", id);
    }

    GameState g = NewActiveGame();
    g.player.slots[0].mag = 0;
    Press(&g, fire);
    CHECK(CountEvents(&g, EVENT_FIRE) == 0, "an empty magazine must not fire");
    CHECK(CountEvents(&g, EVENT_RELOAD) == 1, "an empty magazine starts a reload, events=%d", CountEvents(&g, EVENT_RELOAD));

    GameState full = NewActiveGame();
    Press(&full, reload);
    CHECK(CountEvents(&full, EVENT_RELOAD) == 0, "a full magazine has no reload");

    GameState manual = NewActiveGame();
    manual.player.slots[0].mag = 3;
    Press(&manual, reload);
    CHECK(CountEvents(&manual, EVENT_RELOAD) == 1, "manual reload events=%d", CountEvents(&manual, EVENT_RELOAD));
    Press(&manual, reload);
    CHECK(CountEvents(&manual, EVENT_RELOAD) == 0, "a reload in progress must not start another");
}

static void TestHitAndKillEvents(void)
{
    FrameInput fire = {.fire_pressed = true};
    struct {
        float health;
        float aim_height;
        GameEventKind expected;
    } cases[] = {
        {150.0f, 0.7f, EVENT_HIT},
        {30.0f, 0.7f, EVENT_KILL},
        {150.0f, 1.65f, EVENT_HIT},
        {70.0f, 1.65f, EVENT_HEADSHOT_KILL},
    };
    for (int c = 0; c < (int)(sizeof cases / sizeof cases[0]); c++) {
        GameState g = NewActiveGame();
        Zombie *z = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 4.0f}, cases[c].health);
        AimAt(&g.player, (Vector3){z->pos.x, cases[c].aim_height, z->pos.z});
        Press(&g, fire);
        int total = CountEvents(&g, EVENT_HIT) + CountEvents(&g, EVENT_KILL) + CountEvents(&g, EVENT_HEADSHOT_KILL);
        CHECK(CountEvents(&g, cases[c].expected) == 1 && total == 1, "case %d: expected event %d, hit/kill/headshot=%d/%d/%d", c,
              (int)cases[c].expected, CountEvents(&g, EVENT_HIT), CountEvents(&g, EVENT_KILL), CountEvents(&g, EVENT_HEADSHOT_KILL));
    }

    GameState miss = NewActiveGame();
    PlaceZombie(&miss, (Vector3){0.0f, 0.0f, 4.0f}, 150.0f);
    miss.player.yaw = 3.14159265f;
    Press(&miss, fire);
    CHECK(CountEvents(&miss, EVENT_HIT) == 0, "a miss must not play a hit");
}

static int CountDamageNumbers(const GameState *g)
{
    int count = 0;
    for (int i = 0; i < MAX_DAMAGE_NUMBERS; i++) count += g->damage_numbers[i].timer > 0.0f;
    return count;
}

static const DamageNumber *FirstDamageNumber(const GameState *g)
{
    for (int i = 0; i < MAX_DAMAGE_NUMBERS; i++) {
        if (g->damage_numbers[i].timer > 0.0f) return &g->damage_numbers[i];
    }
    return NULL;
}

static void TestDamageNumbers(void)
{
    FrameInput fire = {.fire_pressed = true};
    FrameInput idle = {0};
    const float damage = WEAPONS[WEAPON_PISTOL].damage;

    GameState body = NewActiveGame();
    Zombie *z = PlaceZombie(&body, (Vector3){0.0f, 0.0f, 4.0f}, 500.0f);
    AimAt(&body.player, (Vector3){z->pos.x, 0.7f, z->pos.z});
    Press(&body, fire);
    const DamageNumber *n = FirstDamageNumber(&body);
    CHECK(CountDamageNumbers(&body) == 1 && n != NULL, "a body hit must create one number, got %d", CountDamageNumbers(&body));
    CHECK(n != NULL && n->amount == damage && !n->headshot && !n->kill && n->zombie == (int)(z - body.zombies),
          "body number amount=%.1f headshot=%d kill=%d", n ? (double)n->amount : -1.0, n ? n->headshot : -1, n ? n->kill : -1);
    CHECK(n != NULL && fabsf(n->pos.x) < 0.01f && fabsf(n->pos.z - z->pos.z) < 0.5f && fabsf(n->pos.y - 0.7f) < 0.2f,
          "the number must start at the hit point, pos=%.2f,%.2f,%.2f", n ? (double)n->pos.x : 0.0, n ? (double)n->pos.y : 0.0, n ? (double)n->pos.z : 0.0);

    GameState head = NewActiveGame();
    z = PlaceZombie(&head, (Vector3){0.0f, 0.0f, 4.0f}, 500.0f);
    AimAt(&head.player, (Vector3){z->pos.x, 1.65f, z->pos.z});
    Press(&head, fire);
    n = FirstDamageNumber(&head);
    CHECK(n != NULL && n->headshot && n->amount == damage * HEADSHOT_MULTIPLIER, "headshot number amount=%.1f", n ? (double)n->amount : -1.0);

    GameState killed = NewActiveGame();
    z = PlaceZombie(&killed, (Vector3){0.0f, 0.0f, 4.0f}, 10.0f);
    AimAt(&killed.player, (Vector3){z->pos.x, 0.7f, z->pos.z});
    Press(&killed, fire);
    n = FirstDamageNumber(&killed);
    CHECK(n != NULL && n->kill, "the killing blow must mark its number");

    GameState blast = NewActiveGame();
    blast.player.slots[0] = (WeaponSlot){WEAPON_SHOTGUN, WEAPONS[WEAPON_SHOTGUN].mag_size, WEAPONS[WEAPON_SHOTGUN].reserve_max};
    z = PlaceZombie(&blast, (Vector3){0.0f, 0.0f, 1.2f}, 9000.0f);
    AimAt(&blast.player, (Vector3){z->pos.x, 0.7f, z->pos.z});
    Press(&blast, fire);
    n = FirstDamageNumber(&blast);
    CHECK(CountDamageNumbers(&blast) == 1 && n != NULL, "pellets on one zombie must merge, got %d numbers", CountDamageNumbers(&blast));
    CHECK(n != NULL && fabsf(n->amount - (9000.0f - z->health)) < 0.01f && n->amount > damage, "merged amount=%.1f health lost=%.1f",
          n ? (double)n->amount : -1.0, (double)(9000.0f - z->health));

    for (float t = 0.0f; t < DAMAGE_NUMBER_TIME + 0.05f; t += DT) GameUpdate(&body, &idle, DT);
    CHECK(CountDamageNumbers(&body) == 0, "numbers must free their slot after DAMAGE_NUMBER_TIME");

    GameState full = NewActiveGame();
    for (int i = 0; i < MAX_DAMAGE_NUMBERS; i++) {
        full.damage_numbers[i] = (DamageNumber){.amount = 1.0f, .timer = 0.1f + 0.01f * (float)i, .zombie = MAX_ZOMBIES - 1};
    }
    z = PlaceZombie(&full, (Vector3){0.0f, 0.0f, 4.0f}, 500.0f);
    AimAt(&full.player, (Vector3){z->pos.x, 0.7f, z->pos.z});
    Press(&full, fire);
    CHECK(CountDamageNumbers(&full) == MAX_DAMAGE_NUMBERS && full.damage_numbers[0].amount == damage && full.damage_numbers[1].amount == 1.0f,
          "a full pool must recycle the oldest number");

    GameState over = NewActiveGame();
    z = PlaceZombie(&over, (Vector3){0.0f, 0.0f, 4.0f}, 500.0f);
    AimAt(&over.player, (Vector3){z->pos.x, 0.7f, z->pos.z});
    Press(&over, fire);
    over.phase = PHASE_GAME_OVER;
    for (float t = 0.0f; t < DAMAGE_NUMBER_TIME + 0.05f; t += DT) GameUpdate(&over, &idle, DT);
    CHECK(CountDamageNumbers(&over) == 0, "numbers must finish during game over");
}

static void TestZombieAndPlayerDamageEvents(void)
{
    FrameInput idle = {0};
    GameState g = NewActiveGame();
    Zombie *z = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 9.0f}, 150.0f);
    z->attack_timer = 0.0f;
    z->growl_timer = 1.0f;

    CHECK(!EventWithin(&g, idle, EVENT_ZOMBIE_GROWL, 30), "a growl must wait for its timer");
    GameState later = NewActiveGame();
    Zombie *growler = PlaceZombie(&later, (Vector3){0.0f, 0.0f, -8.0f}, 150.0f);
    growler->growl_timer = 1.0f;
    CHECK(EventWithin(&later, idle, EVENT_ZOMBIE_GROWL, 70), "no growl after the timer expired");
    const GameEvent *growl = FindEvent(&later, EVENT_ZOMBIE_GROWL);
    CHECK(growl != NULL && growl->arg == (int)(growler - later.zombies), "growl names the zombie, arg=%d", growl ? growl->arg : -1);

    GameState fight = NewActiveGame();
    Zombie *biter = PlaceZombie(&fight, (Vector3){0.0f, 0.0f, 9.0f}, 150.0f);
    biter->attack_timer = 0.0f;
    CHECK(EventWithin(&fight, idle, EVENT_ZOMBIE_ATTACK, 60), "no attack event when a zombie starts to bite");
    CHECK(CountEvents(&fight, EVENT_PLAYER_DAMAGE) == 0, "damage must wait for the wind-up end");
    CHECK(EventWithin(&fight, idle, EVENT_PLAYER_DAMAGE, 60), "no damage event when the bite lands");
}

static void TestRoundEvents(void)
{
    FrameInput idle = {0};
    GameState g;
    GameInit(&g, 5);
    g.phase_timer = INTERMISSION_TIME;
    Press(&g, idle);
    const GameEvent *start = FindEvent(&g, EVENT_ROUND_START);
    CHECK(start != NULL && start->arg == 1 && g.phase == PHASE_ROUND_ACTIVE, "round start event=%d phase=%d", start != NULL, (int)g.phase);
    CHECK(CountEvents(&g, EVENT_ROUND_END) == 0, "round end must wait for the clear");

    g.zombies_left_to_spawn = 0;
    Press(&g, idle);
    const GameEvent *end = FindEvent(&g, EVENT_ROUND_END);
    CHECK(end != NULL && end->arg == 1 && g.round_number == 2, "round end event=%d round=%d", end != NULL, g.round_number);
    Press(&g, idle);
    CHECK(CountEvents(&g, EVENT_ROUND_END) == 0 && CountEvents(&g, EVENT_ROUND_START) == 0, "round events must fire once");
}

static void TestPurchaseEvents(void)
{
    int door_item = FindDoor();
    int cost = LEVEL_DOORS[LEVEL_INTERACTABLES[door_item].door].cost;
    FrameInput interact = {.interact_pressed = true};
    GameState g = NewActiveGame();
    StandNear(&g, door_item);

    g.player.points = cost - 1;
    Press(&g, interact);
    CHECK(CountEvents(&g, EVENT_PURCHASE_DENIED) == 1 && CountEvents(&g, EVENT_PURCHASE) == 0, "short on points: denied=%d purchase=%d",
          CountEvents(&g, EVENT_PURCHASE_DENIED), CountEvents(&g, EVENT_PURCHASE));

    g.player.points = cost;
    Press(&g, interact);
    CHECK(CountEvents(&g, EVENT_PURCHASE) == 1 && CountEvents(&g, EVENT_PURCHASE_DENIED) == 0, "enough points: denied=%d purchase=%d",
          CountEvents(&g, EVENT_PURCHASE_DENIED), CountEvents(&g, EVENT_PURCHASE));

    GameState far = NewActiveGame();
    far.player.points = 0;
    Press(&far, interact);
    CHECK(CountEvents(&far, EVENT_PURCHASE_DENIED) == 0, "pressing E with nothing in reach must be silent");
}

static bool SettingsEqual(const Settings *a, const Settings *b)
{
    for (int i = 0; i < SETTING_COUNT; i++) {
        if (fabsf(SettingsGet(a, (SettingId)i) - SettingsGet(b, (SettingId)i)) > 0.001f) return false;
    }
    return true;
}

static void TestSettingsDefaultsAndAdjust(void)
{
    Settings s = SettingsDefault();
    CHECK(fabsf(s.volume - 0.8f) < 1e-6f && fabsf(s.sensitivity - 1.0f) < 1e-6f && fabsf(s.fov - 75.0f) < 1e-6f,
          "defaults: volume=%f sensitivity=%f fov=%f", (double)s.volume, (double)s.sensitivity, (double)s.fov);

    for (int i = 0; i < SETTING_COUNT; i++) {
        const SettingDef *def = SettingsDef((SettingId)i);
        Settings up = SettingsDefault();
        Settings down = SettingsDefault();
        for (int n = 0; n < 200; n++) {
            SettingsAdjust(&up, (SettingId)i, 1);
            SettingsAdjust(&down, (SettingId)i, -1);
        }
        CHECK(SettingsGet(&up, (SettingId)i) == def->max, "%s clamps at max: %f", def->name, (double)SettingsGet(&up, (SettingId)i));
        CHECK(SettingsGet(&down, (SettingId)i) == def->min, "%s clamps at min: %f", def->name, (double)SettingsGet(&down, (SettingId)i));

        Settings one = SettingsDefault();
        SettingsAdjust(&one, (SettingId)i, 1);
        CHECK(fabsf(SettingsGet(&one, (SettingId)i) - def->default_value - def->step) < 1e-4f ||
                  SettingsGet(&one, (SettingId)i) == def->max,
              "%s steps by %f: %f", def->name, (double)def->step, (double)SettingsGet(&one, (SettingId)i));
    }

    Settings other = SettingsDefault();
    SettingsAdjust(&other, SETTING_FOV, 1);
    CHECK(other.volume == s.volume && other.sensitivity == s.sensitivity, "adjusting fov must leave the other settings alone");
}

static void TestSettingsSetRatio(void)
{
    Settings s = SettingsDefault();
    SettingsSetRatio(&s, SETTING_FOV, 0.0f);
    CHECK(s.fov == 60.0f, "ratio 0 sets min: %f", (double)s.fov);
    SettingsSetRatio(&s, SETTING_FOV, 1.0f);
    CHECK(s.fov == 110.0f, "ratio 1 sets max: %f", (double)s.fov);
    SettingsSetRatio(&s, SETTING_FOV, 0.52f);
    CHECK(s.fov == 85.0f, "ratio snaps to the 5 degree step: %f", (double)s.fov);
    SettingsSetRatio(&s, SETTING_FOV, -3.0f);
    CHECK(s.fov == 60.0f, "ratio below 0 clamps: %f", (double)s.fov);
    SettingsSetRatio(&s, SETTING_VOLUME, 7.0f);
    CHECK(s.volume == 1.0f && s.fov == 60.0f, "ratio above 1 clamps and leaves other settings alone");
}

static void TestSettingsParse(void)
{
    Settings s;
    Settings defaults = SettingsDefault();

    CHECK(SettingsParse("volume=0.5\nsensitivity=2.0\nfov=90\n", &s), "valid text reports success");
    CHECK(fabsf(s.volume - 0.5f) < 1e-6f && fabsf(s.sensitivity - 2.0f) < 1e-6f && fabsf(s.fov - 90.0f) < 1e-6f,
          "valid text: volume=%f sensitivity=%f fov=%f", (double)s.volume, (double)s.sensitivity, (double)s.fov);

    CHECK(SettingsParse("fov=100", &s), "a key without a trailing newline still parses");
    CHECK(s.fov == 100.0f && s.volume == defaults.volume && s.sensitivity == defaults.sensitivity,
          "partial text keeps defaults for missing keys: fov=%f volume=%f", (double)s.fov, (double)s.volume);

    CHECK(!SettingsParse("garbage\n=\nvolume\nvolume=abc\nfov=\nunknown=5\n\n===\n", &s), "garbage reports no keys read");
    CHECK(SettingsEqual(&s, &defaults), "garbage leaves defaults");

    CHECK(SettingsParse("unknown=5\nvolume=0.3\r\nfov=70x\n", &s) && fabsf(s.volume - 0.3f) < 1e-6f && s.fov == defaults.fov,
          "unknown keys and malformed values are skipped, CRLF is accepted: volume=%f fov=%f", (double)s.volume, (double)s.fov);

    SettingsParse("volume=7\nsensitivity=-4\nfov=500\n", &s);
    CHECK(s.volume == 1.0f && s.sensitivity == 0.2f && s.fov == 110.0f, "out-of-range values clamp: volume=%f sensitivity=%f fov=%f",
          (double)s.volume, (double)s.sensitivity, (double)s.fov);

    SettingsParse("volume=nan\nfov=inf\n", &s);
    CHECK(s.volume == defaults.volume && s.fov == 110.0f, "nan falls back to the default and inf clamps: volume=%f fov=%f", (double)s.volume,
          (double)s.fov);

    CHECK(!SettingsParse(NULL, &s) && SettingsEqual(&s, &defaults), "NULL text yields defaults");
    CHECK(!SettingsParse("", &s) && SettingsEqual(&s, &defaults), "empty text yields defaults");
    CHECK(!SettingsParse("volume=0.1", NULL), "NULL output is rejected");
}

static void TestSettingsFormatRoundTrip(void)
{
    Settings original = SettingsDefault();
    SettingsAdjust(&original, SETTING_VOLUME, -1);
    SettingsAdjust(&original, SETTING_SENSITIVITY, 1);
    SettingsAdjust(&original, SETTING_SENSITIVITY, 1);
    SettingsAdjust(&original, SETTING_FOV, -1);

    char buf[256];
    int length = SettingsFormat(&original, buf, sizeof buf);
    CHECK(length > 0 && (size_t)length == strlen(buf), "format length matches the text: %d vs %zu", length, strlen(buf));

    Settings parsed;
    CHECK(SettingsParse(buf, &parsed) && SettingsEqual(&original, &parsed), "format then parse restores the settings");

    char tiny[8];
    int needed = SettingsFormat(&original, tiny, sizeof tiny);
    CHECK(needed == length && strlen(tiny) < sizeof tiny, "a small buffer is truncated, not overrun, and reports the full length: %d", needed);
    CHECK(SettingsFormat(&original, NULL, 0) == length, "a NULL buffer only measures");
}

static const DebugCommand *FindDebugCommand(DebugKind kind, int arg)
{
    for (int i = 0; i < DEBUG_COMMAND_COUNT; i++) {
        if (DEBUG_COMMANDS[i].kind == kind && DEBUG_COMMANDS[i].arg == arg) return &DEBUG_COMMANDS[i];
    }
    return NULL;
}

static void DebugApply(GameState *g, DebugKind kind, int arg)
{
    const DebugCommand *cmd = FindDebugCommand(kind, arg);
    CHECK(cmd != NULL, "no debug command for kind=%d arg=%d", (int)kind, arg);
    if (cmd != NULL) GameDebugApply(g, cmd);
}

static void TestDebugTableCoversEveryWeaponAndPerk(void)
{
    for (int i = 0; i < WEAPON_COUNT; i++) {
        const DebugCommand *cmd = FindDebugCommand(DEBUG_WEAPON, i);
        CHECK(cmd != NULL, "weapon %d has no debug row", i);
        if (cmd != NULL) CHECK(strstr(cmd->label, WEAPONS[i].name) != NULL, "label \"%s\" misses \"%s\"", cmd->label, WEAPONS[i].name);
    }
    for (int i = 0; i < PERK_COUNT; i++) {
        const DebugCommand *cmd = FindDebugCommand(DEBUG_PERK, i);
        CHECK(cmd != NULL, "perk %d has no debug row", i);
        if (cmd != NULL) CHECK(strstr(cmd->label, PERKS[i].name) != NULL, "label \"%s\" misses \"%s\"", cmd->label, PERKS[i].name);
    }
}

static void TestDebugPointsWeaponsAndPerks(void)
{
    GameState g = NewActiveGame();
    DebugApply(&g, DEBUG_POINTS, 5000);
    CHECK(g.player.points == START_POINTS + 5000, "points=%d", g.player.points);

    DebugApply(&g, DEBUG_WEAPON, WEAPON_SMG);
    DebugApply(&g, DEBUG_WEAPON, WEAPON_SHOTGUN);
    CHECK(g.player.slot_count == 2, "slot_count=%d", g.player.slot_count);
    CHECK(g.player.slots[g.player.active].id == WEAPON_SHOTGUN, "active weapon=%d", (int)g.player.slots[g.player.active].id);
    CHECK(g.player.slots[g.player.active].mag == WEAPONS[WEAPON_SHOTGUN].mag_size, "mag=%d", g.player.slots[g.player.active].mag);
    CHECK(g.player.slots[g.player.active].reserve == WEAPONS[WEAPON_SHOTGUN].reserve_max, "reserve=%d", g.player.slots[g.player.active].reserve);

    DebugApply(&g, DEBUG_PERK, PERK_VITALITY);
    CHECK(PlayerHasPerk(&g.player, PERK_VITALITY), "Vitality must be owned");
    CHECK(g.player.max_health == PERK_MAX_HEALTH && g.player.health == PERK_MAX_HEALTH, "health=%.0f/%.0f",
          (double)g.player.health, (double)g.player.max_health);

    float base_reload = ReloadDuration(&g.player, WEAPON_SMG);
    DebugApply(&g, DEBUG_PERK, PERK_QUICK_HANDS);
    CHECK(ReloadDuration(&g.player, WEAPON_SMG) == base_reload * QUICK_HANDS_RELOAD_SCALE, "reload=%.2f base=%.2f",
          (double)ReloadDuration(&g.player, WEAPON_SMG), (double)base_reload);
}

static void TestDebugRefillAndHeal(void)
{
    GameState g = NewActiveGame();
    DebugApply(&g, DEBUG_WEAPON, WEAPON_SMG);
    for (int i = 0; i < g.player.slot_count; i++) {
        g.player.slots[i].mag = 1;
        g.player.slots[i].reserve = 2;
    }
    g.player.reload_timer = 1.0f;
    g.player.health = 10.0f;
    DebugApply(&g, DEBUG_REFILL_AMMO, 0);
    DebugApply(&g, DEBUG_HEAL, 0);
    for (int i = 0; i < g.player.slot_count; i++) {
        const WeaponDef *def = &WEAPONS[g.player.slots[i].id];
        CHECK(g.player.slots[i].mag == def->mag_size && g.player.slots[i].reserve == def->reserve_max, "slot %d mag=%d reserve=%d",
              i, g.player.slots[i].mag, g.player.slots[i].reserve);
    }
    CHECK(g.player.reload_timer == 0.0f, "reload_timer=%.2f", (double)g.player.reload_timer);
    CHECK(g.player.health == g.player.max_health, "health=%.0f", (double)g.player.health);
}

static void TestDebugGodMode(void)
{
    GameState g = NewActiveGame();
    FrameInput idle = {0};
    Zombie *z = PlaceZombie(&g, (Vector3){0.0f, 0.0f, 7.0f}, 150.0f);
    z->attack_timer = 0.0f;

    DebugApply(&g, DEBUG_GOD_MODE, 0);
    for (int i = 0; i < 60 * 10; i++) GameUpdate(&g, &idle, DT);
    CHECK(g.player.health == PLAYER_BASE_HEALTH, "god mode must block damage, health=%.1f", (double)g.player.health);

    DebugApply(&g, DEBUG_GOD_MODE, 0);
    for (int i = 0; i < 60 * 10 && g.player.health == PLAYER_BASE_HEALTH; i++) GameUpdate(&g, &idle, DT);
    CHECK(g.player.health < PLAYER_BASE_HEALTH, "damage must return after toggling off, health=%.1f", (double)g.player.health);
}

static void TestDebugOpenDoors(void)
{
    GameState g = NewActiveGame();
    CHECK(!g.zone_open[1], "the back room starts closed");
    DebugApply(&g, DEBUG_OPEN_DOORS, 0);
    CHECK(g.zone_open[1], "the back room must open");
}

static void TestDebugSkipRound(void)
{
    GameState g = NewActiveGame();
    FrameInput idle = {0};
    g.round_number = 3;
    g.zombies_left_to_spawn = 5;
    PlaceZombie(&g, (Vector3){0.0f, 0.0f, 7.0f}, 150.0f);
    PlaceZombie(&g, (Vector3){3.0f, 0.0f, 9.0f}, 150.0f);
    int points = g.player.points;
    int kills = g.kills;

    DebugApply(&g, DEBUG_SKIP_ROUND, 0);
    for (float t = 0.0f; t < ZOMBIE_DYING_TIME + 0.5f; t += DT) GameUpdate(&g, &idle, DT);
    CHECK(g.round_number == 4, "round_number=%d", g.round_number);
    CHECK(g.phase == PHASE_INTERMISSION, "phase=%d", (int)g.phase);
    CHECK(g.kills == kills && g.player.points == points, "kills=%d points=%d", g.kills, g.player.points);

    DebugApply(&g, DEBUG_SKIP_ROUND, 0);
    GameUpdate(&g, &idle, DT);
    CHECK(g.phase == PHASE_ROUND_ACTIVE, "skipping an intermission must start the round, phase=%d", (int)g.phase);
}

static void TestMuteFlagParsing(void)
{
    char program[] = "zombies";
    char mute[] = "--mute";
    char smoke[] = "--smoke";
    char *neither[] = {program, smoke};
    char *flag[] = {program, smoke, mute};

    unsetenv("ZOMBIES_MUTE");
    CHECK(!AudioMuted(2, neither), "no flag and no ZOMBIES_MUTE must not mute");
    CHECK(AudioMuted(3, flag), "--mute must mute");

    setenv("ZOMBIES_MUTE", "1", 1);
    CHECK(AudioMuted(2, neither), "ZOMBIES_MUTE=1 must mute");
    CHECK(AudioMuted(3, flag), "--mute with ZOMBIES_MUTE=1 must mute");

    setenv("ZOMBIES_MUTE", "0", 1);
    CHECK(!AudioMuted(2, neither), "ZOMBIES_MUTE=0 must not mute");
    setenv("ZOMBIES_MUTE", "", 1);
    CHECK(!AudioMuted(2, neither), "an empty ZOMBIES_MUTE must not mute");
    unsetenv("ZOMBIES_MUTE");
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
    TestQuickHandsPerk();
    TestQuickHandsCannotTrapZombies();
    TestSwapCannotShortenTheFireCooldown();
    TestWeaponSelectionInput();
    TestFullAmmoRefillIsNotSold();
    TestGameOverLetsDyingZombiesFinishFalling();
    TestPlayerCannotWalkThroughZombies();
    TestZombiesNeverStackOnThePlayer();
    TestJump();
    TestNoDoubleJump();
    TestCrouch();
    TestCannotJumpWhileCrouched();
    TestStandingPlayerCannotWalkUnderTheSlab();
    TestCrouchedPlayerWalksUnderTheSlab();
    TestReleasingCrouchUnderTheSlabKeepsThePlayerLow();
    TestTheSlabStopsBullets();
    TestZombiesReachPlayersAtTheSlab();
    TestJumpDoesNotDodgeZombieAttacks();
    TestZombiesStayOnTheFloorWhilePlayerJumps();
    TestInteractablesAreVisibleInPlainView();
    TestPartitionHidesTheFrontRoomFromTheBackRoom();
    TestDoorwayControlsTheViewIntoTheFrontRoom();
    TestFireAndReloadEvents();
    TestHitAndKillEvents();
    TestDamageNumbers();
    TestZombieAndPlayerDamageEvents();
    TestRoundEvents();
    TestPurchaseEvents();
    TestDebugTableCoversEveryWeaponAndPerk();
    TestDebugPointsWeaponsAndPerks();
    TestDebugRefillAndHeal();
    TestDebugGodMode();
    TestDebugOpenDoors();
    TestDebugSkipRound();
    TestSettingsDefaultsAndAdjust();
    TestSettingsSetRatio();
    TestSettingsParse();
    TestSettingsFormatRoundTrip();
    TestMuteFlagParsing();

    if (g_failures > 0) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return EXIT_FAILURE;
    }
    printf("all %d checks passed\n", g_checks);
    return EXIT_SUCCESS;
}
