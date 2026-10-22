#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

int main(void)
{
    TestInit();
    TestShootingAndReload();
    TestHeadshotAndWalls();

    if (g_failures > 0) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return EXIT_FAILURE;
    }
    printf("all %d checks passed\n", g_checks);
    return EXIT_SUCCESS;
}
