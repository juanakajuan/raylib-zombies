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

int main(void)
{
    TestInit();

    if (g_failures > 0) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return EXIT_FAILURE;
    }
    printf("all %d checks passed\n", g_checks);
    return EXIT_SUCCESS;
}
