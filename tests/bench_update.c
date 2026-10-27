#define _POSIX_C_SOURCE 199309L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "game.h"

#define BENCH_DT (1.0f / 240.0f)
#define BENCH_RUNS 7
#define BENCH_STEPS 100000
#define WARMUP_STEP_LIMIT (240 * 120)
#define CIRCLE_CENTER_X 0.0f
#define CIRCLE_CENTER_Z 7.0f
#define CIRCLE_RADIUS 3.0f
#define CIRCLE_SPEED 4.5f

typedef enum Motion {
    MOTION_STILL,
    MOTION_CIRCLE,
} Motion;

static const char *const MOTION_NAMES[] = {"still", "circle"};

static double step_us[BENCH_STEPS];

static int AliveZombies(const GameState *g)
{
    int alive = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (g->zombies[i].state == ZOMBIE_CHASE || g->zombies[i].state == ZOMBIE_ATTACK) alive++;
    }
    return alive;
}

static double NowSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int CompareDoubles(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(void)
{
#ifndef NDEBUG
    fprintf(stderr, "warning: zombies_bench built without NDEBUG, configure with -DCMAKE_BUILD_TYPE=Release\n");
#endif
    const FrameInput idle = {0};
    GameState start;
    GameInit(&start, 1);
    start.phase = PHASE_ROUND_ACTIVE;
    start.god_mode = true;
    for (int z = 0; z < ZONE_COUNT; z++) start.zone_open[z] = true;
    start.zombies_left_to_spawn = MAX_ALIVE_ZOMBIES;
    for (int i = 0; i < WARMUP_STEP_LIMIT && AliveZombies(&start) < MAX_ALIVE_ZOMBIES; i++) GameUpdate(&start, &idle, BENCH_DT);
    if (AliveZombies(&start) != MAX_ALIVE_ZOMBIES) {
        fprintf(stderr, "warmup reached %d of %d chasing zombies\n", AliveZombies(&start), MAX_ALIVE_ZOMBIES);
        return EXIT_FAILURE;
    }

    GameState g = start;
    bool all_alive = true;
    for (Motion motion = MOTION_STILL; motion <= MOTION_CIRCLE; motion++) {
        double us_per_step[BENCH_RUNS];
        double run_worst_us[BENCH_RUNS];
        double worst_p999_us = 0.0;
        long floods = 0;
        for (int run = 0; run < BENCH_RUNS; run++) {
            g = start;
            double total = 0.0;
            for (int i = 0; i < BENCH_STEPS; i++) {
                if (motion == MOTION_CIRCLE) {
                    float angle = (float)i * BENCH_DT * CIRCLE_SPEED / CIRCLE_RADIUS;
                    g.player.pos = (Vector3){CIRCLE_CENTER_X + CIRCLE_RADIUS * cosf(angle), 0.0f, CIRCLE_CENTER_Z + CIRCLE_RADIUS * sinf(angle)};
                }
                int goal = g.nav.goal;
                double begin = NowSeconds();
                GameUpdate(&g, &idle, BENCH_DT);
                step_us[i] = (NowSeconds() - begin) * 1e6;
                total += step_us[i];
                floods += g.nav.goal != goal;
            }
            us_per_step[run] = total / BENCH_STEPS;
            qsort(step_us, BENCH_STEPS, sizeof step_us[0], CompareDoubles);
            double p999_us = step_us[BENCH_STEPS - BENCH_STEPS / 1000];
            run_worst_us[run] = step_us[BENCH_STEPS - 1];
            if (p999_us > worst_p999_us) worst_p999_us = p999_us;
            printf("scenario=%s run=%d us_per_step=%.3f p999_us=%.1f worst_us=%.1f\n", MOTION_NAMES[motion], run + 1, us_per_step[run], p999_us,
                   step_us[BENCH_STEPS - 1]);
        }
        qsort(us_per_step, BENCH_RUNS, sizeof us_per_step[0], CompareDoubles);
        qsort(run_worst_us, BENCH_RUNS, sizeof run_worst_us[0], CompareDoubles);
        printf("scenario=%s median_us_per_step=%.3f p999_us=%.1f median_run_worst_us=%.1f max_worst_us=%.1f floods_per_run=%ld alive_zombies=%d steps=%d "
               "runs=%d\n",
               MOTION_NAMES[motion], us_per_step[BENCH_RUNS / 2], worst_p999_us, run_worst_us[BENCH_RUNS / 2], run_worst_us[BENCH_RUNS - 1],
               floods / BENCH_RUNS, AliveZombies(&g), BENCH_STEPS, BENCH_RUNS);
        all_alive = all_alive && AliveZombies(&g) == MAX_ALIVE_ZOMBIES;
    }

    double flood_us[NAV_CELLS];
    int goals = 0;
    NavField field = start.nav;
    for (int c = 0; c < NAV_CELLS; c++) {
        if (!field.walkable[c]) continue;
        double begin = NowSeconds();
        NavFlood(&field, c);
        flood_us[goals++] = (NowSeconds() - begin) * 1e6;
    }
    qsort(flood_us, (size_t)goals, sizeof flood_us[0], CompareDoubles);
    GameState rebuilt = start;
    rebuilt.nav.ready = false;
    double begin = NowSeconds();
    GameUpdateNav(&rebuilt);
    printf("nav flood_median_us=%.1f flood_worst_us=%.1f goals=%d walkable_and_flood_us=%.1f\n", flood_us[goals / 2], flood_us[goals - 1], goals,
           (NowSeconds() - begin) * 1e6);
    return all_alive ? EXIT_SUCCESS : EXIT_FAILURE;
}
