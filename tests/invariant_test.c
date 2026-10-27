/* Seeded random input drives the sim, and the invariants are checked after every step.
   Usage: invariant_test [seeds] [steps]. A failure prints the seed and step that reproduce it. */
#define _POSIX_C_SOURCE 199309L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"
#include "raymath.h"

#define DT (1.0f / 240.0f)
#define EPS 1e-4f
#define DEFAULT_SEEDS 8
#define DEFAULT_STEPS 50000L
#define MAX_TURN 0.15f

typedef enum DriverMode {
    DRIVE_WANDER,
    DRIVE_INTERACT,
    DRIVE_HUNT,
    DRIVE_MODE_COUNT,
} DriverMode;

typedef struct Driver {
    uint32_t rng;
    DriverMode mode;
    int mode_steps;
    int target; /* interactable index for DRIVE_INTERACT */
    Vector2 wander;
    bool fire_run;
    bool crouch_run;
    bool sprint_run;
} Driver;

static uint32_t Next(Driver *d)
{
    uint32_t x = d->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    d->rng = x;
    return x;
}

static float Uniform(Driver *d) { return (float)(Next(d) >> 8) / 16777216.0f; }
static bool Chance(Driver *d, float p) { return Uniform(d) < p; }
static int Pick(Driver *d, int n) { return (int)(Next(d) % (uint32_t)n); }

/* Yaw and pitch deltas toward a world point, clamped like a fast mouse. */
static Vector2 LookToward(const Player *p, Vector3 target)
{
    Vector3 eye = PlayerEye(p);
    float dx = target.x - eye.x, dy = target.y - eye.y, dz = target.z - eye.z;
    float yaw = remainderf(atan2f(dx, -dz) - p->yaw, 2.0f * PI);
    float pitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) - p->pitch;
    return (Vector2){Clamp(yaw, -MAX_TURN, MAX_TURN), Clamp(pitch, -MAX_TURN, MAX_TURN)};
}

static FrameInput NextInput(Driver *d, const GameState *g, int *debug_cmd)
{
    const Player *p = &g->player;
    FrameInput in = {0};
    if (d->mode_steps-- <= 0) {
        d->mode = (DriverMode)Pick(d, DRIVE_MODE_COUNT);
        d->mode_steps = 60 + Pick(d, 900);
        d->target = Pick(d, LEVEL_INTERACTABLE_COUNT);
        d->wander = (Vector2){Uniform(d) * 2.0f - 1.0f, Uniform(d) * 2.0f - 1.0f};
    }
    if (Chance(d, 0.01f)) d->fire_run = !d->fire_run;
    if (Chance(d, 0.004f)) d->crouch_run = !d->crouch_run;
    if (Chance(d, 0.004f)) d->sprint_run = !d->sprint_run;

    switch (d->mode) {
    case DRIVE_WANDER:
        in.move = d->wander;
        in.look = (Vector2){(Uniform(d) - 0.5f) * 0.05f, (Uniform(d) - 0.5f) * 0.02f};
        break;
    case DRIVE_INTERACT: {
        Vector3 c = BoxCenter(LEVEL_INTERACTABLES[d->target].box);
        float dist = hypotf(c.x - p->pos.x, c.z - p->pos.z);
        in.look = LookToward(p, c);
        in.move = (Vector2){(Uniform(d) - 0.5f) * 0.6f, dist > 1.5f ? 1.0f : 0.0f};
        in.interact_pressed = dist < 3.0f && Chance(d, 0.05f);
        break;
    }
    case DRIVE_HUNT: {
        int best = -1;
        float best_dist = INFINITY;
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            const Zombie *z = &g->zombies[i];
            if (z->state != ZOMBIE_CHASE && z->state != ZOMBIE_ATTACK) continue;
            float dist = Vector3Distance(z->pos, p->pos);
            if (dist < best_dist) best_dist = dist, best = i;
        }
        if (best >= 0) {
            Vector3 aim = g->zombies[best].pos;
            aim.y = Chance(d, 0.5f) ? 1.65f : 1.0f;
            in.look = LookToward(p, aim);
            in.move = (Vector2){Uniform(d) - 0.5f, best_dist > 4.0f ? 0.5f : -0.5f};
        }
        in.fire_pressed = Chance(d, 0.1f);
        in.fire_held = Chance(d, 0.3f);
        break;
    }
    case DRIVE_MODE_COUNT:
        break;
    }
    in.fire_held = in.fire_held || d->fire_run;
    in.fire_pressed = in.fire_pressed || Chance(d, 0.01f);
    in.reload_pressed = Chance(d, 0.004f);
    in.interact_pressed = in.interact_pressed || Chance(d, 0.002f);
    if (Chance(d, 0.004f)) in.select_slot = Pick(d, MAX_SLOTS + 2);
    if (Chance(d, 0.004f)) in.cycle_steps = Pick(d, 5) - 2;
    in.sprint_held = d->sprint_run;
    in.crouch_held = d->crouch_run;
    in.jump_pressed = Chance(d, 0.003f);
    in.restart_pressed = Chance(d, 0.002f);
    *debug_cmd = Chance(d, 0.0005f) ? Pick(d, DEBUG_COMMAND_COUNT) : -1;
    return in;
}

static bool HasEvent(const GameState *g, GameEventKind kind)
{
    for (int i = 0; i < g->event_count; i++) {
        if (g->events[i].kind == kind) return true;
    }
    return false;
}

static bool HitThisStep(const GameState *g)
{
    return HasEvent(g, EVENT_HIT) || HasEvent(g, EVENT_KILL) || HasEvent(g, EVENT_HEADSHOT_KILL);
}

static bool UnderOverhang(const Player *p)
{
    float top = p->pos.y + p->eye_height + PLAYER_HEAD_CLEARANCE;
    for (int i = 0; i < LEVEL_OVERHANG_COUNT; i++) {
        const BoundingBox *b = &LEVEL_OVERHANGS[i];
        if (p->pos.x + PLAYER_RADIUS > b->min.x && p->pos.x - PLAYER_RADIUS < b->max.x &&
            p->pos.z + PLAYER_RADIUS > b->min.z && p->pos.z - PLAYER_RADIUS < b->max.z &&
            top > b->min.y + EPS && p->pos.y < b->max.y) {
            return true;
        }
    }
    return false;
}

typedef struct Violation {
    char msg[256];
} Violation;

#define VIOLATE(...) do { snprintf(v->msg, sizeof v->msg, __VA_ARGS__); return false; } while (0)

/* Rules that hold for every reachable GameState. */
static bool CheckState(const GameState *g, Violation *v)
{
    const Player *p = &g->player;
    if (!isfinite(p->health) || !isfinite(p->pos.x) || !isfinite(p->pos.y) || !isfinite(p->pos.z)) VIOLATE("player health or position is not finite");
    if (p->health < 0.0f || p->health > p->max_health + EPS) VIOLATE("health %.3f outside [0, %.1f]", p->health, p->max_health);
    if (p->points < 0) VIOLATE("points %d < 0", p->points);
    if (p->slot_count < 1 || p->slot_count > MAX_SLOTS || p->active < 0 || p->active >= p->slot_count) VIOLATE("slot_count %d active %d", p->slot_count, p->active);
    for (int i = 0; i < p->slot_count; i++) {
        const WeaponSlot *s = &p->slots[i];
        const WeaponDef *w = &WEAPONS[s->id];
        if (s->mag < 0 || s->mag > w->mag_size) VIOLATE("slot %d mag %d outside [0, %d]", i, s->mag, w->mag_size);
        if (s->reserve < 0 || s->reserve > w->reserve_max) VIOLATE("slot %d reserve %d outside [0, %d]", i, s->reserve, w->reserve_max);
        for (int j = 0; j < i; j++) {
            if (p->slots[j].id == s->id) VIOLATE("weapon %d in two slots", s->id);
        }
    }
    if (GameBlocked(g, p->pos, PLAYER_RADIUS)) VIOLATE("player inside a solid at %.2f,%.2f", p->pos.x, p->pos.z);
    if (UnderOverhang(p)) VIOLATE("player body inside the overhang");
    if (p->pos.y < 0.0f) VIOLATE("player below the floor");
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        const Zombie *z = &g->zombies[i];
        if (z->state == ZOMBIE_FREE) continue;
        if (!isfinite(z->pos.x) || !isfinite(z->pos.z)) VIOLATE("zombie %d position is not finite", i);
        if (GameBlocked(g, z->pos, ZOMBIE_RADIUS)) VIOLATE("zombie %d inside a solid at %.2f,%.2f", i, z->pos.x, z->pos.z);
    }
    return true;
}

static bool SlotsEqual(const Player *a, const Player *b)
{
    return a->slot_count == b->slot_count && memcmp(a->slots, b->slots, sizeof a->slots) == 0;
}

static bool CheckTimer(const char *name, float before, float after, float dt, Violation *v)
{
    float want = fmaxf(0.0f, before - dt);
    if (fabsf(after - want) > EPS) VIOLATE("%s %.4f -> %.4f, want %.4f", name, before, after, want);
    return true;
}

/* Rules for one GameUpdate step from pre to post. */
static bool CheckStep(const GameState *pre, const GameState *post, float dt, Violation *v)
{
    if (pre->phase == PHASE_GAME_OVER && post->phase != PHASE_GAME_OVER) return true; /* restart rebuilds the state */
    const Player *a = &pre->player, *b = &post->player;
    bool game_over = pre->phase == PHASE_GAME_OVER;

    int spent = 0, purchases = 0;
    for (int i = 0; i < post->event_count; i++) {
        if (post->events[i].kind != EVENT_PURCHASE) continue;
        purchases++;
        spent += GameDescribeInteractable(pre, post->events[i].arg).cost;
    }
    if (b->points < a->points - spent) VIOLATE("points fell %d -> %d with %d spent", a->points, b->points, spent);
    if (!HitThisStep(post) && b->points != a->points - spent) VIOLATE("points %d -> %d with %d spent and no hit", a->points, b->points, spent);
    /* A shot after the purchase in the same step spends one round of a refill. */
    Player undo = *b;
    for (int i = 0; i < post->event_count; i++) undo.slots[undo.active].mag += post->events[i].kind == EVENT_FIRE;
    if (purchases > 0 && SlotsEqual(a, &undo) && a->perks == b->perks &&
        memcmp(pre->zone_open, post->zone_open, sizeof pre->zone_open) == 0) {
        VIOLATE("purchase with no effect (%d points spent)", spent);
    }

    /* A buy equips the weapon, and a swap in the same step can go back to the old slot. */
    bool swapped = a->active != b->active || purchases > 0;
    float ticked = game_over ? a->fire_cooldown : fmaxf(0.0f, a->fire_cooldown - dt);
    float ceiling = swapped ? fmaxf(ticked, SWAP_TIME) : ticked;
    for (int i = 0; i < post->event_count; i++) {
        if (post->events[i].kind == EVENT_FIRE) ceiling = fmaxf(ceiling, WEAPONS[post->events[i].arg].fire_interval);
    }
    if (b->fire_cooldown < ticked - EPS) VIOLATE("fire_cooldown fell %.4f -> %.4f in dt %.4f", a->fire_cooldown, b->fire_cooldown, dt);
    if (b->fire_cooldown > ceiling + EPS) VIOLATE("fire_cooldown rose %.4f -> %.4f, ceiling %.4f", a->fire_cooldown, b->fire_cooldown, ceiling);
    if (b->reload_timer > a->reload_timer + EPS && !HasEvent(post, EVENT_RELOAD)) VIOLATE("reload_timer rose without a reload");

    /* Effect timers fade what is on screen, so they tick in every phase, game over included. */
    if (!HitThisStep(post) && !CheckTimer("hitmarker_timer", a->hitmarker_timer, b->hitmarker_timer, dt, v)) return false;
    if (!HasEvent(post, EVENT_FIRE) && !CheckTimer("muzzle_timer", a->muzzle_timer, b->muzzle_timer, dt, v)) return false;
    if (!HasEvent(post, EVENT_PLAYER_DAMAGE) && !CheckTimer("damage_flash", a->damage_flash, b->damage_flash, dt, v)) return false;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        const Zombie *za = &pre->zombies[i], *zb = &post->zombies[i];
        bool hit_flash_reset = zb->hit_flash >= ZOMBIE_HIT_FLASH_TIME - dt - EPS && HitThisStep(post);
        if (!hit_flash_reset && !CheckTimer("zombie hit_flash", za->hit_flash, zb->hit_flash, dt, v)) return false;
        if (za->state == ZOMBIE_DYING && zb->state == ZOMBIE_DYING && fabsf(zb->dying_timer - (za->dying_timer - dt)) > EPS) {
            VIOLATE("zombie %d dying_timer %.4f -> %.4f", i, za->dying_timer, zb->dying_timer);
        }
    }
    for (int i = 0; i < MAX_DAMAGE_NUMBERS; i++) {
        float before = pre->damage_numbers[i].timer, after = post->damage_numbers[i].timer;
        if (!(HitThisStep(post) && after == DAMAGE_NUMBER_TIME) && !CheckTimer("damage number timer", before, after, dt, v)) return false;
    }
    return true;
}

typedef struct RunStats {
    long steps, game_overs, restarts, purchases, kills, debug_cmds;
} RunStats;

/* Returns the 1-based step of the first violation, or 0 when every step passes. */
static long Run(uint32_t seed, long max_steps, Violation *v, RunStats *stats)
{
    static GameState g, pre;
    Driver d = {.rng = seed * 2654435761u + 1u};
    GameInit(&g, seed);
    for (long step = 1; step <= max_steps; step++) {
        int cmd;
        FrameInput in = NextInput(&d, &g, &cmd);
        if (cmd >= 0) {
            GameDebugApply(&g, &DEBUG_COMMANDS[cmd]);
            stats->debug_cmds++;
            if (!CheckState(&g, v)) return step;
        }
        pre = g;
        GameUpdate(&g, &in, DT);
        stats->steps++;
        stats->game_overs += g.phase == PHASE_GAME_OVER && pre.phase != PHASE_GAME_OVER;
        stats->restarts += pre.phase == PHASE_GAME_OVER && g.phase != PHASE_GAME_OVER;
        stats->purchases += HasEvent(&g, EVENT_PURCHASE);
        stats->kills += HasEvent(&g, EVENT_KILL) || HasEvent(&g, EVENT_HEADSHOT_KILL);
        if (!CheckState(&g, v) || !CheckStep(&pre, &g, DT, v)) return step;
    }
    return 0;
}

static double NowSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Parses a positive count, or returns 0 when the text is not one. */
static long ParseCount(const char *text)
{
    char *end;
    long value = strtol(text, &end, 10);
    return end != text && *end == '\0' && value > 0 ? value : 0;
}

int main(int argc, char **argv)
{
    long seeds = argc > 1 ? ParseCount(argv[1]) : DEFAULT_SEEDS;
    long steps = argc > 2 ? ParseCount(argv[2]) : DEFAULT_STEPS;
    if (argc > 3 || seeds <= 0 || seeds > UINT32_MAX || steps <= 0) {
        fprintf(stderr, "usage: invariant_test [seeds] [steps], both positive integers\n");
        return EXIT_FAILURE;
    }
    int failures = 0;
    RunStats sum = {0};
    double start = NowSeconds();
    for (long s = 1; s <= seeds; s++) {
        Violation v = {0};
        long at = Run((uint32_t)s, steps, &v, &sum);
        if (at > 0) {
            failures++;
            printf("FAIL seed %ld step %ld: %s\n", s, at, v.msg);
        }
    }
    double secs = NowSeconds() - start;
    printf("%ld seeds, %ld steps, %.3f s; game overs %ld, restarts %ld, purchases %ld, kills %ld, debug commands %ld; %d failing seeds\n",
           seeds, sum.steps, secs, sum.game_overs, sum.restarts, sum.purchases, sum.kills, sum.debug_cmds, failures);
    return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
