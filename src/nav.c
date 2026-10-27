#include "nav.h"

#include <math.h>
#include <stdlib.h>

#include "raymath.h"

#define NAV_SEED_RADIUS 2

#define LOOKAHEAD_CELLS 3

static const int STEP_DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
static const int STEP_DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
static const uint16_t STEP_COST[8] = {10, 10, 10, 10, 14, 14, 14, 14};

int NavCellAt(Vector3 pos)
{
    int col = (int)floorf((pos.x - NAV_MIN_X) / NAV_CELL_SIZE);
    int row = (int)floorf((pos.z - NAV_MIN_Z) / NAV_CELL_SIZE);
    col = col < 0 ? 0 : col >= NAV_COLS ? NAV_COLS - 1 : col;
    row = row < 0 ? 0 : row >= NAV_ROWS ? NAV_ROWS - 1 : row;
    return row * NAV_COLS + col;
}

Vector3 NavCellCenter(int cell)
{
    return (Vector3){NAV_MIN_X + ((float)(cell % NAV_COLS) + 0.5f) * NAV_CELL_SIZE, 0.0f,
                     NAV_MIN_Z + ((float)(cell / NAV_COLS) + 0.5f) * NAV_CELL_SIZE};
}

static int StepCell(int cell, int k)
{
    int col = cell % NAV_COLS + STEP_DX[k];
    int row = cell / NAV_COLS + STEP_DZ[k];
    if (col < 0 || col >= NAV_COLS || row < 0 || row >= NAV_ROWS) return -1;
    return row * NAV_COLS + col;
}

static bool CornerClear(const NavField *f, int cell, int k)
{
    if (k < 4) return true;
    int col = cell % NAV_COLS;
    int row = cell / NAV_COLS;
    return f->walkable[row * NAV_COLS + col + STEP_DX[k]] && f->walkable[(row + STEP_DZ[k]) * NAV_COLS + col];
}

#define QUEUE_BUCKETS 64
_Static_assert(QUEUE_BUCKETS > NAV_SEED_RADIUS * 15 + 14, "every queued cost must fit in one turn of the buckets");

typedef struct CostQueue {
    int count;
    int head[QUEUE_BUCKETS];
    int next[NAV_CELLS];
    int prev[NAV_CELLS];
    bool queued[NAV_CELLS];
} CostQueue;

static void QueueUnlink(CostQueue *q, int bucket, int cell)
{
    if (q->prev[cell] >= 0) q->next[q->prev[cell]] = q->next[cell];
    else q->head[bucket] = q->next[cell];
    if (q->next[cell] >= 0) q->prev[q->next[cell]] = q->prev[cell];
    q->queued[cell] = false;
    q->count--;
}

static void QueueLower(CostQueue *q, uint16_t *cost, int cell, uint16_t value)
{
    if (q->queued[cell]) QueueUnlink(q, cost[cell] % QUEUE_BUCKETS, cell);
    cost[cell] = value;
    int bucket = value % QUEUE_BUCKETS;
    q->prev[cell] = -1;
    q->next[cell] = q->head[bucket];
    if (q->head[bucket] >= 0) q->prev[q->head[bucket]] = cell;
    q->head[bucket] = cell;
    q->queued[cell] = true;
    q->count++;
}

void NavFlood(NavField *f, int goal)
{
    CostQueue q;
    q.count = 0;
    for (int b = 0; b < QUEUE_BUCKETS; b++) q.head[b] = -1;
    for (int c = 0; c < NAV_CELLS; c++) {
        f->cost[c] = NAV_UNREACHED;
        q.queued[c] = false;
    }
    Vector3 goal_center = NavCellCenter(goal);
    for (int dz = -NAV_SEED_RADIUS; dz <= NAV_SEED_RADIUS; dz++) {
        for (int dx = -NAV_SEED_RADIUS; dx <= NAV_SEED_RADIUS; dx++) {
            int col = goal % NAV_COLS + dx;
            int row = goal / NAV_COLS + dz;
            if (col < 0 || col >= NAV_COLS || row < 0 || row >= NAV_ROWS) continue;
            int c = row * NAV_COLS + col;
            if (!f->walkable[c]) continue;
            QueueLower(&q, f->cost, c, (uint16_t)(Vector3Distance(NavCellCenter(c), goal_center) / NAV_CELL_SIZE * 10.0f));
        }
    }
    for (int bucket = 0; q.count > 0; bucket = (bucket + 1) % QUEUE_BUCKETS) {
        while (q.head[bucket] >= 0) {
            int cell = q.head[bucket];
            QueueUnlink(&q, bucket, cell);
            for (int k = 0; k < 8; k++) {
                int next = StepCell(cell, k);
                if (next < 0 || !f->walkable[next] || !CornerClear(f, cell, k)) continue;
                uint16_t through = (uint16_t)(f->cost[cell] + STEP_COST[k]);
                if (through < f->cost[next]) QueueLower(&q, f->cost, next, through);
            }
        }
    }
    f->goal = goal;
}

/* The lowest-cost cell one step from cell, or cell itself when no step goes down. */
static int Downhill(const NavField *f, int cell)
{
    int best = cell;
    uint16_t best_cost = f->walkable[cell] ? f->cost[cell] : NAV_UNREACHED;
    for (int k = 0; k < 8; k++) {
        int next = StepCell(cell, k);
        if (next < 0 || !f->walkable[next]) continue;
        if (f->walkable[cell] && !CornerClear(f, cell, k)) continue;
        if (f->cost[next] < best_cost) {
            best_cost = f->cost[next];
            best = next;
        }
    }
    return best;
}

/* Walks every cell the segment crosses. Where it passes exactly through a corner, both side cells must be walkable. */
static bool LineWalkable(const NavField *f, Vector3 from, Vector3 to)
{
    int cell = NavCellAt(from);
    int col = cell % NAV_COLS;
    int row = cell / NAV_COLS;
    int end = NavCellAt(to);
    int steps = abs(end % NAV_COLS - col) + abs(end / NAV_COLS - row);
    float dx = to.x - from.x;
    float dz = to.z - from.z;
    int step_col = dx > 0.0f ? 1 : -1;
    int step_row = dz > 0.0f ? 1 : -1;
    float delta_x = dx != 0.0f ? NAV_CELL_SIZE / fabsf(dx) : INFINITY;
    float delta_z = dz != 0.0f ? NAV_CELL_SIZE / fabsf(dz) : INFINITY;
    float edge_x = NAV_MIN_X + (float)(col + (step_col > 0)) * NAV_CELL_SIZE;
    float edge_z = NAV_MIN_Z + (float)(row + (step_row > 0)) * NAV_CELL_SIZE;
    float next_x = dx != 0.0f ? (edge_x - from.x) / dx : INFINITY;
    float next_z = dz != 0.0f ? (edge_z - from.z) / dz : INFINITY;
    while (steps > 0) {
        if (next_x < next_z) {
            col += step_col;
            next_x += delta_x;
            steps--;
        } else if (next_z < next_x) {
            row += step_row;
            next_z += delta_z;
            steps--;
        } else {
            if (!f->walkable[row * NAV_COLS + col + step_col] || !f->walkable[(row + step_row) * NAV_COLS + col]) return false;
            col += step_col;
            row += step_row;
            next_x += delta_x;
            next_z += delta_z;
            steps -= 2;
        }
        if (!f->walkable[row * NAV_COLS + col]) return false;
    }
    return row * NAV_COLS + col == end;
}

/* Aims at a cell centre and never at the player, because a player flush with a face stands on the plane where a zombie can freeze. */
Vector3 NavDirection(const NavField *f, Vector3 from, Vector3 target)
{
    int here = NavCellAt(from);
    int aim = Downhill(f, here);
    if (here == f->goal || aim == here || f->cost[aim] == NAV_UNREACHED) return Vector3Normalize(Vector3Subtract(target, from));
    for (int k = 0; k < LOOKAHEAD_CELLS && aim != f->goal; k++) {
        int further = Downhill(f, aim);
        if (further == aim || !LineWalkable(f, from, NavCellCenter(further))) break;
        aim = further;
    }
    return Vector3Normalize(Vector3Subtract(NavCellCenter(aim), from));
}
