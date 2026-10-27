#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "game.h"
#include "input.h"

#define BENCH_DT (1.0f / 240.0f)
#define BENCH_RUNS 7
#define BENCH_STEPS 100000
#define WARMUP_STEP_LIMIT (240 * 120)
#define ROUTE_FRAMES 200000

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

/* A busy frame of play: walk, sprint, fire and reload. Returns the median microseconds per RouteInput call. */
static double BenchRouteInput(void)
{
    KeySnapshot keys = {.mouse_delta = {3.0f, -2.0f}};
    const int held[] = {KEY_W, KEY_D, KEY_LEFT_SHIFT, INPUT_MOUSE_LEFT};
    for (size_t i = 0; i < sizeof held / sizeof held[0]; i++) keys.code[held[i]] = INPUT_DOWN;
    keys.code[KEY_R] = INPUT_DOWN | INPUT_PRESSED;
    double us_per_frame[BENCH_RUNS];
    for (int run = 0; run < BENCH_RUNS; run++) {
        double begin = NowSeconds();
        for (int i = 0; i < ROUTE_FRAMES; i++) {
            keys.mouse_delta.x = (float)(i & 7);
            RouteInput(OVERLAY_NONE, false, &keys, 0.002f);
        }
        us_per_frame[run] = (NowSeconds() - begin) * 1e6 / ROUTE_FRAMES;
    }
    qsort(us_per_frame, BENCH_RUNS, sizeof us_per_frame[0], CompareDoubles);
    return us_per_frame[BENCH_RUNS / 2];
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

    double us_per_step[BENCH_RUNS];
    GameState g = start;
    for (int run = 0; run < BENCH_RUNS; run++) {
        g = start;
        double begin = NowSeconds();
        for (int i = 0; i < BENCH_STEPS; i++) GameUpdate(&g, &idle, BENCH_DT);
        us_per_step[run] = (NowSeconds() - begin) * 1e6 / BENCH_STEPS;
        printf("run=%d us_per_step=%.3f\n", run + 1, us_per_step[run]);
    }
    qsort(us_per_step, BENCH_RUNS, sizeof us_per_step[0], CompareDoubles);
    printf("median_us_per_step=%.3f alive_zombies=%d steps=%d runs=%d\n", us_per_step[BENCH_RUNS / 2], AliveZombies(&g),
           BENCH_STEPS, BENCH_RUNS);
    printf("median_us_per_route=%.3f frames=%d runs=%d\n", BenchRouteInput(), ROUTE_FRAMES, BENCH_RUNS);
    return AliveZombies(&g) == MAX_ALIVE_ZOMBIES ? EXIT_SUCCESS : EXIT_FAILURE;
}
